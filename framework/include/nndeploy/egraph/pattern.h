#ifndef _NNDEPLOY_EGRAPH_PATTERN_H_
#define _NNDEPLOY_EGRAPH_PATTERN_H_

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "nndeploy/egraph/egraph.h"
#include "nndeploy/egraph/recexpr.h"
#include "nndeploy/egraph/subst.h"

namespace nndeploy {
namespace egraph {

// ---------------------------------------------------------------------------
// ENodeOrVar<Op>: tagged union — either an ENode<Op> or a Var.
// Mirrors egg's ENodeOrVar<L>.
// ---------------------------------------------------------------------------

template <typename Op>
class ENodeOrVar {
 public:
  enum class Tag { kENode, kVar };

  // Construct from an ENode.
  static ENodeOrVar fromENode(const ENode<Op>& node) {
    ENodeOrVar result;
    result.tag_ = Tag::kENode;
    result.node_ = node;
    return result;
  }

  // Construct from a Var.
  static ENodeOrVar fromVar(const Var& var) {
    ENodeOrVar result;
    result.tag_ = Tag::kVar;
    result.var_ = var;
    return result;
  }

  Tag tag() const { return tag_; }

  bool isENode() const { return tag_ == Tag::kENode; }
  bool isVar() const { return tag_ == Tag::kVar; }

  const ENode<Op>& enode() const {
    if (tag_ != Tag::kENode) {
      throw std::logic_error("ENodeOrVar is not an ENode");
    }
    return node_;
  }

  ENode<Op>& enode() {
    if (tag_ != Tag::kENode) {
      throw std::logic_error("ENodeOrVar is not an ENode");
    }
    return node_;
  }

  const Var& var() const {
    if (tag_ != Tag::kVar) {
      throw std::logic_error("ENodeOrVar is not a Var");
    }
    return var_;
  }

  // Children — only meaningful for ENode variant; Var has none.
  const std::vector<Id>& children() const {
    if (tag_ == Tag::kENode) {
      return node_.children;
    }
    static const std::vector<Id> empty;
    return empty;
  }

  std::vector<Id>& children() {
    if (tag_ != Tag::kENode) {
      throw std::logic_error("cannot get mutable children of a Var");
    }
    return node_.children;
  }

  bool operator==(const ENodeOrVar& other) const {
    if (tag_ != other.tag_) return false;
    if (tag_ == Tag::kENode) return node_ == other.node_;
    return var_ == other.var_;
  }

  ENodeOrVar() : tag_(Tag::kVar) {}

 private:
  Tag tag_;
  ENode<Op> node_;
  Var var_;
};

// Small parser-free construction helpers.
// These reduce boilerplate when authoring PatternAst / ENodeOrVar trees.

template <typename Op>
ENodeOrVar<Op> makePatternVar(const Var& var) {
  return ENodeOrVar<Op>::fromVar(var);
}

template <typename Op>
ENodeOrVar<Op> makePatternVar(const std::string& var_name) {
  return ENodeOrVar<Op>::fromVar(Var::fromString(var_name));
}

template <typename Op>
ENodeOrVar<Op> makePatternNode(const Op& op,
                               std::initializer_list<Id> children = {}) {
  ENode<Op> node;
  node.op = op;
  node.children.assign(children.begin(), children.end());
  return ENodeOrVar<Op>::fromENode(node);
}

// ---------------------------------------------------------------------------
// PatternAst<Op>: a RecExpr over ENodeOrVar<Op>.
// Topologically ordered just like RecExpr, but nodes can be Var leaves.
// ---------------------------------------------------------------------------

template <typename Op>
class Pattern;

template <typename Op>
using PatternAst = RecExpr<ENodeOrVar<Op>>;

template <typename Op>
class PatternBuilder {
 public:
  using Node = ENodeOrVar<Op>;

  PatternBuilder() = default;
  explicit PatternBuilder(PatternAst<Op> ast) : ast_(std::move(ast)) {}

  Id appendVar(const Var& var) { return append(makePatternVar<Op>(var)); }

  Id appendVar(const std::string& var_name) {
    return append(makePatternVar<Op>(var_name));
  }

  Id appendNode(const Op& op, std::initializer_list<Id> children = {}) {
    return append(makePatternNode<Op>(op, children));
  }

  Id append(const Node& payload) {
    if (payload.isENode()) {
      return append(payload, payload.enode().children);
    }
    return append(payload, {});
  }

  Id append(const Node& payload, std::initializer_list<Id> children) {
    return append(payload, std::vector<Id>(children));
  }

  Id append(const Node& payload, const std::vector<Id>& children) {
    if (payload.isVar() && !children.empty()) {
      throw std::invalid_argument(
          "pattern variable must be a leaf (no children)");
    }

    ENode<Node> pat_node;
    pat_node.op = payload;
    pat_node.children = children;
    return ast_.add(std::move(pat_node));
  }

  const PatternAst<Op>& ast() const { return ast_; }

  PatternAst<Op> buildAst() const& { return ast_; }
  PatternAst<Op> buildAst() && { return std::move(ast_); }

  Pattern<Op> buildPattern() const& { return Pattern<Op>(ast_); }
  Pattern<Op> buildPattern() && { return Pattern<Op>(std::move(ast_)); }

 private:
  PatternAst<Op> ast_;
};

// ---------------------------------------------------------------------------
// SearchMatches<Op>: result of searching one e-class.
// ---------------------------------------------------------------------------

template <typename Op>
struct SearchMatches {
  Id eclass;
  std::vector<Subst> substs;
};

// ---------------------------------------------------------------------------
// Pattern<Op>: wraps a PatternAst and provides search/matching.
// ---------------------------------------------------------------------------

template <typename Op>
class Pattern {
 public:
  using Node = ENodeOrVar<Op>;

  explicit Pattern(PatternAst<Op> ast) : ast_(std::move(ast)) {
    validate();
    collectVars();
  }

  // Build a pattern from a plain RecExpr<Op> (no variables — concrete expr).
  static Pattern fromExpr(const RecExpr<Op>& expr) {
    std::vector<ENode<ENodeOrVar<Op>>> pat_nodes;
    pat_nodes.reserve(expr.size());
    for (std::size_t i = 0; i < expr.size(); ++i) {
      const auto& node = expr.at(i);
      ENodeOrVar<Op> payload = ENodeOrVar<Op>::fromENode(node);
      // The children of the original ENode become children of the pattern node.
      // In a PatternAst, the "children" field of the ENode<ENodeOrVar<Op>>
      // wrapper stores the references to earlier PatternAst entries, same as
      // the original RecExpr.
      ENode<ENodeOrVar<Op>> pat_node;
      pat_node.op = payload;
      pat_node.children = node.children;  // same index-based references
      pat_nodes.push_back(std::move(pat_node));
    }
    return Pattern(PatternAst<Op>(std::move(pat_nodes)));
  }

  // Accessors.
  const PatternAst<Op>& ast() const { return ast_; }
  const std::vector<Var>& vars() const { return vars_; }

  // ------------------------------------------------------------------
  // search: find all matches of this pattern in an entire egraph.
  // Returns a vector of SearchMatches, one per matched e-class root.
  // ------------------------------------------------------------------
  template <typename Analysis, typename OpHash>
  std::vector<SearchMatches<Op>> search(
      EGraph<Op, Analysis, OpHash>& egraph) const {
    std::vector<SearchMatches<Op>> results;
    for (const auto& root_id : egraph.roots()) {
      auto matches = searchEclass(egraph, root_id);
      if (!matches.substs.empty()) {
        results.push_back(std::move(matches));
      }
    }
    return results;
  }

  // ------------------------------------------------------------------
  // searchEclass: find all substitutions that match this pattern in
  // the given e-class.
  // ------------------------------------------------------------------
  template <typename Analysis, typename OpHash>
  SearchMatches<Op> searchEclass(EGraph<Op, Analysis, OpHash>& egraph,
                                 Id eclass_id) const {
    SearchMatches<Op> result;
    result.eclass = egraph.find(eclass_id);

    // Start matching from the root of the pattern AST.
    Id root_pat = ast_.rootId();
    std::vector<Subst> substs;
    substs.push_back(Subst(vars_.size()));

    matchNode(egraph, root_pat, result.eclass, substs);

    result.substs = std::move(substs);
    return result;
  }

  // ------------------------------------------------------------------
  // searchEclassWithSubsts: find all substitutions that match this
  // pattern in the given e-class, starting from pre-populated
  // substitutions.  This is used by MultiPattern to share variable
  // bindings across multiple patterns.
  // ------------------------------------------------------------------
  template <typename Analysis, typename OpHash>
  SearchMatches<Op> searchEclassWithSubsts(
      EGraph<Op, Analysis, OpHash>& egraph, Id eclass_id,
      std::vector<Subst> initial_substs) const {
    SearchMatches<Op> result;
    result.eclass = egraph.find(eclass_id);

    Id root_pat = ast_.rootId();
    matchNode(egraph, root_pat, result.eclass, initial_substs);

    result.substs = std::move(initial_substs);
    return result;
  }

 private:
  // Validate: Var nodes must be leaves (no children).
  void validate() const {
    for (std::size_t i = 0; i < ast_.size(); ++i) {
      const auto& wrapper = ast_.at(i);
      const auto& payload = wrapper.op;
      if (payload.isVar() && !wrapper.children.empty()) {
        throw std::invalid_argument(
            "pattern variable must be a leaf (no children)");
      }
    }
  }

  // Collect unique variables in the pattern.
  void collectVars() {
    vars_.clear();
    for (std::size_t i = 0; i < ast_.size(); ++i) {
      const auto& payload = ast_.at(i).op;
      if (payload.isVar()) {
        const Var& v = payload.var();
        bool found = false;
        for (const auto& existing : vars_) {
          if (existing == v) {
            found = true;
            break;
          }
        }
        if (!found) {
          vars_.push_back(v);
        }
      }
    }
  }

  // ------------------------------------------------------------------
  // Recursive matching engine.
  // matchNode tries to match pattern node `pat_id` against egraph
  // e-class `eclass_id`.  `substs` is the current set of candidate
  // substitutions; matching filters/extends them in place.
  // ------------------------------------------------------------------
  template <typename Analysis, typename OpHash>
  void matchNode(EGraph<Op, Analysis, OpHash>& egraph, Id pat_id, Id eclass_id,
                 std::vector<Subst>& substs) const {
    if (substs.empty()) return;

    eclass_id = egraph.find(eclass_id);
    const auto& wrapper = ast_[pat_id];
    const auto& payload = wrapper.op;

    if (payload.isVar()) {
      // Variable node: bind or check consistency.
      const Var& var = payload.var();
      std::vector<Subst> next;
      next.reserve(substs.size());
      for (auto& s : substs) {
        const Id* bound = s.get(var);
        if (bound == nullptr) {
          // Bind the variable.
          s.insert(var, eclass_id);
          next.push_back(std::move(s));
        } else if (egraph.find(*bound) == eclass_id) {
          // Already bound to the same canonical class — keep.
          next.push_back(std::move(s));
        }
        // Otherwise, mismatch — drop this substitution.
      }
      substs = std::move(next);
      return;
    }

    // ENode pattern: match against every enode in the e-class.
    const auto& pat_enode = payload.enode();
    const auto* eclass = egraph.getEClass(eclass_id);
    if (eclass == nullptr) {
      substs.clear();
      return;
    }

    // For each current substitution, try every node in the class.
    std::vector<Subst> next;
    for (const auto& s : substs) {
      for (const auto& candidate : eclass->nodes) {
        // Check operator and arity match.
        if (!(candidate.op == pat_enode.op)) continue;
        if (candidate.children.size() != wrapper.children.size()) continue;

        // Start with a copy of the current substitution and
        // recursively match each child.
        std::vector<Subst> child_substs;
        child_substs.push_back(s);

        bool failed = false;
        for (std::size_t c = 0; c < wrapper.children.size(); ++c) {
          matchNode(egraph, wrapper.children[c], candidate.children[c],
                    child_substs);
          if (child_substs.empty()) {
            failed = true;
            break;
          }
        }

        if (!failed) {
          for (auto& cs : child_substs) {
            next.push_back(std::move(cs));
          }
        }
      }
    }
    substs = std::move(next);
  }

  PatternAst<Op> ast_;
  std::vector<Var> vars_;
};

}  // namespace egraph
}  // namespace nndeploy

#endif
