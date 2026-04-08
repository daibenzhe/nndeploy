#ifndef _NNDEPLOY_EGRAPH_EGRAPH_H_
#define _NNDEPLOY_EGRAPH_EGRAPH_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Forward declarations for RecExpr used by addExpr overload.
namespace nndeploy {
namespace egraph {
template <typename Op>
class RecExpr;

// Forward declarations for explanation output types (defined in explain.h).
template <typename Op>
class Explanation;
template <typename Op>
struct TreeTerm;
}  // namespace egraph
}  // namespace nndeploy

namespace nndeploy {
namespace egraph {

struct Id {
  std::size_t value;

  Id() : value(invalidValue()) {}
  explicit Id(std::size_t v) : value(v) {}

  static constexpr std::size_t invalidValue() {
    return std::numeric_limits<std::size_t>::max();
  }

  bool valid() const { return value != invalidValue(); }

  bool operator==(const Id &other) const { return value == other.value; }
  bool operator!=(const Id &other) const { return value != other.value; }
  bool operator<(const Id &other) const { return value < other.value; }
};

struct IdHash {
  std::size_t operator()(const Id &id) const { return id.value; }
};

inline std::ostream &operator<<(std::ostream &os, const Id &id) {
  os << id.value;
  return os;
}

class UnionFind {
 public:
  Id makeSet() {
    const std::size_t index = parent_.size();
    parent_.push_back(index);
    rank_.push_back(0);
    return Id(index);
  }

  Id find(Id id) {
    if (!id.valid()) {
      throw std::out_of_range("invalid egraph id");
    }
    if (id.value >= parent_.size()) {
      throw std::out_of_range("egraph id out of range");
    }
    auto &parent = parent_[id.value];
    if (parent != id.value) {
      parent = find(Id(parent)).value;
    }
    return Id(parent);
  }

  Id unite(Id lhs, Id rhs) {
    lhs = find(lhs);
    rhs = find(rhs);
    if (lhs == rhs) {
      return lhs;
    }
    if (rank_[lhs.value] < rank_[rhs.value]) {
      std::swap(lhs, rhs);
    }
    parent_[rhs.value] = lhs.value;
    if (rank_[lhs.value] == rank_[rhs.value]) {
      ++rank_[lhs.value];
    }
    return lhs;
  }

  std::size_t size() const { return parent_.size(); }

  bool contains(Id id) const { return id.valid() && id.value < parent_.size(); }

 private:
  std::vector<std::size_t> parent_;
  std::vector<std::size_t> rank_;
};

template <typename Op>
struct ENode {
  Op op;
  std::vector<Id> children;

  bool operator==(const ENode &other) const {
    return op == other.op && children == other.children;
  }
};

template <typename Op, typename OpHash = std::hash<Op>>
struct ENodeHash {
  std::size_t operator()(const ENode<Op> &node) const {
    std::size_t seed = OpHash{}(node.op);
    for (const auto &child : node.children) {
      seed ^= child.value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    }
    return seed;
  }
};

struct NoAnalysis {
  struct Data {};

  template <typename Node>
  Data make(const Node &) const {
    return Data{};
  }

  template <typename DataType>
  void merge(DataType &, const DataType &) const {}
};

// ---------------------------------------------------------------------------
// Justification: why two e-nodes were merged.
//
// Mirrors egg::Justification.
// Defined here so EGraph can use it without circular includes.
// ---------------------------------------------------------------------------

enum class JustificationKind {
  Rule,        // A named rewrite rule.
  Congruence,  // Children became equal, so parents are congruent.
};

struct Justification {
  JustificationKind kind;
  std::string rule_name;  // Only meaningful for Rule.

  static Justification Rule(std::string name) {
    return Justification{JustificationKind::Rule, std::move(name)};
  }
  static Justification Congruence() {
    return Justification{JustificationKind::Congruence, {}};
  }

  bool isRule() const { return kind == JustificationKind::Rule; }
  bool isCongruence() const { return kind == JustificationKind::Congruence; }
};

// ---------------------------------------------------------------------------
// Connection: a directed edge in the proof graph between two e-nodes.
//
// Mirrors egg's Connection struct.
// ---------------------------------------------------------------------------

struct Connection {
  Id next;                      // The other e-node in this edge.
  Id current;                   // This e-node.
  Justification justification;  // Why they were merged.
  bool is_rewrite_forward;      // Direction of the rewrite.

  Connection()
      : next(),
        current(),
        justification(Justification::Congruence()),
        is_rewrite_forward(true) {}

  Connection(Id next_, Id current_, Justification just, bool forward)
      : next(next_),
        current(current_),
        justification(std::move(just)),
        is_rewrite_forward(forward) {}
};

// ---------------------------------------------------------------------------
// ExplainNode: one node in the proof forest.
//
// Mirrors egg's ExplainNode.
// ---------------------------------------------------------------------------

struct ExplainNode {
  std::vector<Connection> neighbors;  // All connections (tree + shortcuts).
  Connection parent_connection;       // The tree edge to parent.
};

// ---------------------------------------------------------------------------
// Explain<Op, OpHash>: the proof forest that tracks justifications.
//
// Mirrors egg's Explain<L>.
//
// This is a separate data structure from the EGraph.  The EGraph holds an
// Explain<Op, OpHash> and delegates to it during add/merge/rebuild.
// ---------------------------------------------------------------------------

template <typename Op, typename OpHash = std::hash<Op>>
class Explain {
 public:
  using Node = ENode<Op>;

  Explain() = default;

  // ------------------------------------------------------------------
  // Proof forest maintenance (called by EGraph).
  // ------------------------------------------------------------------

  // Register a new e-node in the proof forest.
  void addNode(Id id) {
    if (id.value >= explainfind_.size()) {
      explainfind_.resize(id.value + 1);
    }
    // Self-loop: this node is its own root.
    auto &en = explainfind_[id.value];
    en.parent_connection =
        Connection(id, id, Justification::Congruence(), true);
  }

  // Record a union in the proof forest.
  // node1 and node2 are the ORIGINAL (not canonical) e-node ids.
  void recordUnion(Id node1, Id node2, Justification just) {
    // Make node1 the root of its tree.
    makeLeader(node1);

    // Create the connections.
    Connection fwd(node2, node1, just, true);
    Connection rev(node1, node2, std::move(just), false);

    // Set node1's parent to node2.
    explainfind_[node1.value].parent_connection = fwd;

    // Add neighbor links.
    explainfind_[node1.value].neighbors.push_back(fwd);
    explainfind_[node2.value].neighbors.push_back(std::move(rev));
  }

  // Record an alternate rewrite (shortcut edge) when nodes are already
  // equivalent but a new, potentially shorter proof is discovered.
  void alternateRewrite(Id node1, Id node2, Justification just) {
    Connection fwd(node2, node1, just, true);
    Connection rev(node1, node2, std::move(just), false);
    explainfind_[node1.value].neighbors.push_back(std::move(fwd));
    explainfind_[node2.value].neighbors.push_back(std::move(rev));
  }

  // ------------------------------------------------------------------
  // Proof reconstruction.
  // ------------------------------------------------------------------

  // Get the path from left to right through the proof forest.
  // Returns a list of connections forming the path.
  std::vector<Connection> getPathUnoptimized(Id left, Id right) {
    if (left == right) return {};

    Id ancestor = commonAncestor(left, right);

    // Collect path from left up to ancestor.
    std::vector<Connection> left_path;
    {
      Id cur = left;
      while (cur != ancestor) {
        const auto &conn = explainfind_[cur.value].parent_connection;
        left_path.push_back(conn);
        cur = conn.next;
      }
    }

    // Collect path from right up to ancestor (we'll reverse them).
    std::vector<Connection> right_path;
    {
      Id cur = right;
      while (cur != ancestor) {
        const auto &conn = explainfind_[cur.value].parent_connection;
        // Reverse the connection direction.
        Connection rev;
        rev.next = conn.current;
        rev.current = conn.next;
        rev.justification = conn.justification;
        rev.is_rewrite_forward = !conn.is_rewrite_forward;
        right_path.push_back(std::move(rev));
        cur = conn.next;
      }
    }

    // Concatenate: left_path + reversed(right_path).
    std::vector<Connection> full_path = std::move(left_path);
    for (auto it = right_path.rbegin(); it != right_path.rend(); ++it) {
      full_path.push_back(std::move(*it));
    }

    return full_path;
  }

  // Get the size of the proof forest.
  std::size_t size() const { return explainfind_.size(); }

 private:
  // ------------------------------------------------------------------
  // make_leader: reverse parent pointers to make `node` the root.
  // ------------------------------------------------------------------
  void makeLeader(Id node) {
    // Collect the path from node to the current root.
    std::vector<Id> path;
    Id current = node;
    while (true) {
      path.push_back(current);
      Id parent = explainfind_[current.value].parent_connection.next;
      if (parent == current) break;
      current = parent;
    }

    // Reverse parent pointers along the path.
    // path = [node, p1, p2, ..., old_root]
    // We want: old_root -> ... -> p1 -> node (root)
    for (std::size_t i = path.size() - 1; i > 0; --i) {
      Id child = path[i];       // was closer to old root
      Id parent = path[i - 1];  // was closer to node

      // The old connection FROM child was child -> path[i-1] ... toward root.
      // We reverse it: child now points to parent (closer to node).
      auto &old_conn = explainfind_[child.value].parent_connection;

      Connection new_conn;
      new_conn.next = parent;
      new_conn.current = child;
      new_conn.justification = old_conn.justification;
      new_conn.is_rewrite_forward = !old_conn.is_rewrite_forward;

      explainfind_[child.value].parent_connection = new_conn;
    }

    // Make node the root (self-loop).
    explainfind_[node.value].parent_connection =
        Connection(node, node, Justification::Congruence(), true);
  }

  // ------------------------------------------------------------------
  // Find common ancestor in the proof forest (uncompressed).
  // ------------------------------------------------------------------
  Id commonAncestor(Id left, Id right) const {
    std::unordered_set<std::size_t> seen;

    Id l = left;
    Id r = right;
    bool l_done = false;
    bool r_done = false;

    // Alternate climbing from left and right.
    while (true) {
      if (!l_done) {
        if (!seen.insert(l.value).second) {
          return l;  // Already seen from the other side.
        }
        Id parent = explainfind_[l.value].parent_connection.next;
        if (parent == l) {
          l_done = true;
        } else {
          l = parent;
        }
      }

      if (!r_done) {
        if (!seen.insert(r.value).second) {
          return r;
        }
        Id parent = explainfind_[r.value].parent_connection.next;
        if (parent == r) {
          r_done = true;
        } else {
          r = parent;
        }
      }

      if (l_done && r_done) {
        // Both reached their roots.  They should share a root.
        Id lr = left;
        while (explainfind_[lr.value].parent_connection.next != lr) {
          lr = explainfind_[lr.value].parent_connection.next;
        }
        return lr;
      }
    }
  }

  // ------------------------------------------------------------------
  // Internal state.
  // ------------------------------------------------------------------

  // One ExplainNode per e-node Id ever allocated.
  std::vector<ExplainNode> explainfind_;
};

template <typename Op, typename Analysis = NoAnalysis>
struct EClass {
  using Node = ENode<Op>;
  using Data = typename Analysis::Data;

  Id id;
  std::vector<Node> nodes;
  std::vector<Id> parents;
  Data data;
};

template <typename Op, typename Analysis = NoAnalysis,
          typename OpHash = std::hash<Op>>
class EGraph {
 public:
  using Node = ENode<Op>;
  using Class = EClass<Op, Analysis>;
  using Data = typename Analysis::Data;
  using Memo = std::unordered_map<Node, Id, ENodeHash<Op, OpHash>>;

  explicit EGraph(Analysis analysis = Analysis())
      : analysis_(std::move(analysis)) {}

  // ------------------------------------------------------------------
  // Explanation opt-in.
  // ------------------------------------------------------------------

  // Enable explanation tracking.  Must be called before adding anything.
  EGraph &withExplanationsEnabled() {
    if (union_find_.size() > 0) {
      throw std::logic_error(
          "withExplanationsEnabled() must be called before adding any nodes");
    }
    explain_ = std::make_unique<Explain<Op, OpHash>>();
    return *this;
  }

  // ------------------------------------------------------------------
  // add / addExpr
  // ------------------------------------------------------------------

  Id add(const Node &node) {
    validateChildren(node);
    Node canonical = canonicalizeNode(node);
    const auto memo_it = memo_.find(canonical);

    if (memo_it != memo_.end()) {
      if (explain_) {
        // When explanations are enabled and the node already exists,
        // we still create a NEW Id for this exact node, then merge it
        // with the existing one via congruence.  This preserves the
        // exact syntactic form for proof reconstruction.
        Id existing = find(memo_it->second);
        Id new_id = union_find_.makeSet();

        // Store the original (non-canonical) node.
        if (new_id.value >= nodes_.size()) {
          nodes_.resize(new_id.value + 1);
        }
        nodes_[new_id.value] = node;
        explain_->addNode(new_id);

        // Store in uncanon_memo.
        uncanon_memo_[node] = new_id;

        // Merge with justification: congruence.
        performUnion(existing, new_id,
                     Justification{JustificationKind::Congruence, {}});

        return existing;
      }
      return find(memo_it->second);
    }

    const Id id = union_find_.makeSet();
    Class eclass;
    eclass.id = id;
    eclass.nodes.push_back(canonical);
    eclass.data = analysis_.make(canonical);

    classes_.emplace(id, eclass);
    memo_.emplace(canonical, id);
    for (const auto &child : canonical.children) {
      auto child_root = find(child);
      classes_.at(child_root).parents.push_back(id);
    }

    if (explain_) {
      // Store the original (non-canonical) node.
      if (id.value >= nodes_.size()) {
        nodes_.resize(id.value + 1);
      }
      nodes_[id.value] = node;
      explain_->addNode(id);
      uncanon_memo_[node] = id;
    }

    return id;
  }

  Id addExpr(const Node &root, const std::vector<Node> &subexpressions) {
    std::vector<Id> ids;
    ids.reserve(subexpressions.size());
    for (const auto &subexpr : subexpressions) {
      ids.push_back(add(subexpr));
    }

    Node resolved = root;
    for (auto &child : resolved.children) {
      if (!child.valid() || child.value >= ids.size()) {
        throw std::out_of_range("expression child index out of range");
      }
      child = ids[child.value];
    }
    return add(resolved);
  }

  // Add a complete RecExpr bottom-up into the e-graph.
  // Each node in the RecExpr is added in topological order; child Ids
  // (which are indices into the RecExpr) are remapped to live e-graph Ids.
  // NOTE: RecExpr is forward-declared; its definition lives in recexpr.h.
  // Callers must include recexpr.h before calling this overload.
  Id addExpr(const RecExpr<Op> &expr) {
    std::vector<Id> id_map;
    id_map.reserve(expr.size());
    for (std::size_t i = 0; i < expr.size(); ++i) {
      const auto &src = expr.at(i);
      Node resolved;
      resolved.op = src.op;
      resolved.children.reserve(src.children.size());
      for (const auto &child : src.children) {
        if (!child.valid() || child.value >= id_map.size()) {
          throw std::out_of_range("RecExpr child index out of range");
        }
        resolved.children.push_back(id_map[child.value]);
      }
      id_map.push_back(add(resolved));
    }
    if (id_map.empty()) {
      throw std::out_of_range("cannot addExpr from empty RecExpr");
    }
    return id_map.back();
  }

  // ------------------------------------------------------------------
  // find / merge
  // ------------------------------------------------------------------

  Id find(Id id) { return union_find_.find(id); }

  // Standard merge (no justification).
  Id merge(Id lhs, Id rhs) {
    if (explain_) {
      return mergeWithJustification(
          lhs, rhs, Justification{JustificationKind::Congruence, {}});
    }
    return mergeInternal(lhs, rhs);
  }

  // Merge with a named rule justification (for explanation tracking).
  Id mergeWithJustification(Id lhs, Id rhs, Justification just) {
    Id lhs_canon = find(lhs);
    Id rhs_canon = find(rhs);
    if (lhs_canon == rhs_canon) {
      if (explain_ && just.isRule()) {
        // Already equal, but record alternate rewrite for shorter proofs.
        explain_->alternateRewrite(lhs, rhs, std::move(just));
      }
      return lhs_canon;
    }

    if (explain_) {
      performUnion(lhs, rhs, std::move(just));
    }
    return mergeInternal(lhs_canon, rhs_canon);
  }

  // Convenience: merge with a named rule string.
  Id unionTrusted(Id lhs, Id rhs, const std::string &rule_name) {
    return mergeWithJustification(
        lhs, rhs, Justification{JustificationKind::Rule, rule_name});
  }

  // ------------------------------------------------------------------
  // rebuild
  // ------------------------------------------------------------------

  void rebuild() {
    if (dirty_roots_.empty()) {
      return;
    }

    bool changed = false;
    do {
      changed = false;
      Memo rebuilt_memo;
      std::unordered_map<Id, Class, IdHash> rebuilt_classes;

      for (auto &entry : classes_) {
        Id root = find(entry.first);
        auto &target = rebuilt_classes[root];
        if (!target.id.valid()) {
          target.id = root;
          target.data = entry.second.data;
        } else {
          analysis_.merge(target.data, entry.second.data);
        }

        for (auto node : entry.second.nodes) {
          for (auto &child : node.children) {
            child = find(child);
          }
          auto memo_it = rebuilt_memo.find(node);
          if (memo_it == rebuilt_memo.end()) {
            rebuilt_memo.emplace(node, root);
            target.nodes.push_back(std::move(node));
          } else {
            Id lhs = find(memo_it->second);
            Id rhs = find(root);
            if (lhs != rhs) {
              if (explain_) {
                // Record congruence justification for rebuild-induced merges.
                performUnion(lhs, rhs,
                             Justification{JustificationKind::Congruence, {}});
              }
              union_find_.unite(lhs, rhs);
              changed = true;
            }
          }
        }
      }

      if (!changed) {
        for (auto &entry : rebuilt_classes) {
          entry.second.parents.clear();
        }
        for (auto &entry : rebuilt_classes) {
          for (const auto &node : entry.second.nodes) {
            for (auto child : node.children) {
              child = find(child);
              rebuilt_classes[child].parents.push_back(entry.first);
            }
          }
        }
        classes_ = std::move(rebuilt_classes);
        memo_ = std::move(rebuilt_memo);
      }
    } while (changed);

    dirty_roots_.clear();
  }

  // ------------------------------------------------------------------
  // Explanation access (low-level).
  // For the user-facing explanation API, use the free functions
  // explainIdEquivalence() and explainEquivalence() defined in explain.h.
  // ------------------------------------------------------------------

  // Check if explanations are enabled.
  bool explanationsEnabled() const { return explain_ != nullptr; }

  // Access the proof forest.  Throws if explanations are not enabled.
  Explain<Op, OpHash> &explain() {
    if (!explain_) {
      throw std::logic_error("explanations are not enabled");
    }
    return *explain_;
  }

  // Look up the Id for the root of a RecExpr.
  // When explanations are enabled, uses the uncanon_memo (which stores the
  // original syntactic Ids used during add()).  Falls back to the canonical
  // memo otherwise.  Throws std::invalid_argument if any node in the
  // expression is not found in the e-graph.
  Id lookupRecExpr(const RecExpr<Op> &expr) {
    std::vector<Id> id_map;
    id_map.reserve(expr.size());
    for (std::size_t i = 0; i < expr.size(); ++i) {
      const auto &src = expr.at(i);
      Node resolved;
      resolved.op = src.op;
      resolved.children.reserve(src.children.size());
      for (const auto &child : src.children) {
        if (!child.valid() || child.value >= id_map.size()) {
          throw std::out_of_range("RecExpr child index out of range");
        }
        resolved.children.push_back(id_map[child.value]);
      }

      // Look up this node in the uncanon_memo or regular memo.
      if (explain_) {
        auto it = uncanon_memo_.find(resolved);
        if (it != uncanon_memo_.end()) {
          id_map.push_back(it->second);
          continue;
        }
      }

      // Fall back to canonical lookup.
      Node canonical = canonicalizeNode(resolved);
      auto it = memo_.find(canonical);
      if (it == memo_.end()) {
        throw std::invalid_argument("expression not found in e-graph");
      }
      id_map.push_back(it->second);
    }
    if (id_map.empty()) {
      throw std::out_of_range("cannot lookup empty RecExpr");
    }
    return id_map.back();
  }

  // ------------------------------------------------------------------
  // Accessors.
  // ------------------------------------------------------------------

  // Returns whether the canonical class for `id` is currently alive.
  // Invalid or foreign ids still follow the module-wide error contract and
  // throw via find(id) rather than silently returning false.
  bool contains(Id id) { return classes_.find(find(id)) != classes_.end(); }

  const Class *getEClass(Id id) const {
    auto it = classes_.find(const_cast<EGraph *>(this)->find(id));
    return it == classes_.end() ? nullptr : &it->second;
  }

  std::size_t classCount() const { return classes_.size(); }
  std::size_t memoSize() const { return memo_.size(); }

  std::vector<Id> roots() const {
    std::vector<Id> result;
    result.reserve(classes_.size());
    for (const auto &entry : classes_) {
      result.push_back(entry.first);
    }
    std::sort(result.begin(), result.end());
    return result;
  }

  // Access the internal node storage (for explanation).
  const std::vector<Node> &nodeStorage() const { return nodes_; }

 private:
  // ------------------------------------------------------------------
  // Internal merge (no justification recording).
  // ------------------------------------------------------------------
  Id mergeInternal(Id lhs, Id rhs) {
    lhs = find(lhs);
    rhs = find(rhs);
    if (lhs == rhs) {
      return lhs;
    }

    Id root = union_find_.unite(lhs, rhs);
    Id merged = root == lhs ? rhs : lhs;

    Class &root_class = classes_.at(root);
    Class merged_class = std::move(classes_.at(merged));

    root_class.nodes.insert(root_class.nodes.end(), merged_class.nodes.begin(),
                            merged_class.nodes.end());
    root_class.parents.insert(root_class.parents.end(),
                              merged_class.parents.begin(),
                              merged_class.parents.end());
    analysis_.merge(root_class.data, merged_class.data);
    classes_.erase(merged);
    dirty_roots_.insert(root);
    return root;
  }

  // ------------------------------------------------------------------
  // Record a union in the explanation proof forest.
  // ------------------------------------------------------------------
  void performUnion(Id node1, Id node2, Justification just) {
    if (explain_) {
      explain_->recordUnion(node1, node2, std::move(just));
    }
  }

  void validateChildren(const Node &node) {
    for (const auto &child : node.children) {
      if (!union_find_.contains(child)) {
        throw std::out_of_range("child id is not part of this egraph");
      }
      Id root = union_find_.find(child);
      if (classes_.find(root) == classes_.end()) {
        throw std::out_of_range("child eclass is not alive in this egraph");
      }
    }
  }

  Node canonicalizeNode(const Node &node) {
    Node canonical = node;
    for (auto &child : canonical.children) {
      child = find(child);
    }
    return canonical;
  }

  Analysis analysis_;
  UnionFind union_find_;
  std::unordered_map<Id, Class, IdHash> classes_;
  Memo memo_;
  std::unordered_set<Id, IdHash> dirty_roots_;

  // Explanation-related state (only populated when explanations are enabled).
  std::unique_ptr<Explain<Op, OpHash>> explain_;
  std::vector<Node> nodes_;  // Every e-node ever created, indexed by Id.
  // Non-canonicalized memo: maps original nodes to their Ids.
  std::unordered_map<Node, Id, ENodeHash<Op, OpHash>> uncanon_memo_;
};

}  // namespace egraph
}  // namespace nndeploy

#endif
