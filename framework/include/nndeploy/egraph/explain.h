#ifndef _NNDEPLOY_EGRAPH_EXPLAIN_H_
#define _NNDEPLOY_EGRAPH_EXPLAIN_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "nndeploy/egraph/egraph.h"
#include "nndeploy/egraph/recexpr.h"

namespace nndeploy {
namespace egraph {

// ---------------------------------------------------------------------------
// TreeTerm<Op>: a compact proof step in tree form.
//
// Mirrors egg::TreeTerm<L>.
//
// Each TreeTerm represents one intermediate term in the proof.
// A rewrite step is annotated with forward_rule or backward_rule.
// For congruence steps, child_proofs contains a sub-proof for each child
// position showing how the child was transformed.
// ---------------------------------------------------------------------------

template <typename Op>
struct TreeTerm {
  ENode<Op> node;             // The operator node.
  std::string backward_rule;  // Rule rewriting this back to previous term.
  std::string forward_rule;   // Rule rewriting previous to this term.
  std::vector<std::vector<std::shared_ptr<TreeTerm<Op>>>> child_proofs;
  // One sub-proof per child position.

  bool hasBackwardRule() const { return !backward_rule.empty(); }
  bool hasForwardRule() const { return !forward_rule.empty(); }
};

// ---------------------------------------------------------------------------
// Type alias for a tree explanation (sequence of tree proof steps).
// ---------------------------------------------------------------------------

template <typename Op>
using TreeExplanation = std::vector<std::shared_ptr<TreeTerm<Op>>>;

// ---------------------------------------------------------------------------
// FlatTerm<Op>: a fully expanded term in a flat proof.
//
// Mirrors egg::FlatTerm<L>.
//
// In a FlatExplanation, each consecutive pair of FlatTerms differs by
// exactly one rewrite step, annotated somewhere in the tree with a
// forward_rule or backward_rule.
// ---------------------------------------------------------------------------

template <typename Op>
struct FlatTerm {
  ENode<Op> node;
  std::string backward_rule;
  std::string forward_rule;
  std::vector<FlatTerm<Op>> children;  // One FlatTerm per child position.

  bool hasBackwardRule() const { return !backward_rule.empty(); }
  bool hasForwardRule() const { return !forward_rule.empty(); }

  // Check recursively if any node in this tree has a forward rule.
  bool hasRewriteForward() const {
    if (hasForwardRule()) return true;
    for (const auto& c : children) {
      if (c.hasRewriteForward()) return true;
    }
    return false;
  }

  // Check recursively if any node in this tree has a backward rule.
  bool hasRewriteBackward() const {
    if (hasBackwardRule()) return true;
    for (const auto& c : children) {
      if (c.hasRewriteBackward()) return true;
    }
    return false;
  }

  // Strip all rule annotations recursively.
  void removeRewrites() {
    forward_rule.clear();
    backward_rule.clear();
    for (auto& c : children) {
      c.removeRewrites();
    }
  }

  // Convert to RecExpr.
  RecExpr<Op> getRecExpr() const {
    RecExpr<Op> expr;
    buildRecExpr(expr);
    return expr;
  }

 private:
  Id buildRecExpr(RecExpr<Op>& expr) const {
    std::vector<Id> child_ids;
    child_ids.reserve(children.size());
    for (const auto& c : children) {
      child_ids.push_back(c.buildRecExpr(expr));
    }
    ENode<Op> n;
    n.op = node.op;
    n.children = std::move(child_ids);
    return expr.add(n);
  }
};

template <typename Op>
using FlatExplanation = std::vector<FlatTerm<Op>>;

// ---------------------------------------------------------------------------
// Explanation<Op>: the user-facing result of explain_equivalence.
//
// Mirrors egg::Explanation<L>.
//
// Contains a tree representation (compact, shared sub-proofs) and
// lazily computes a flat representation (one rewrite per step).
// ---------------------------------------------------------------------------

template <typename Op>
class Explanation {
 public:
  explicit Explanation(TreeExplanation<Op> trees) : trees_(std::move(trees)) {}

  // Access the tree representation.
  const TreeExplanation<Op>& trees() const { return trees_; }

  // Get the flat explanation (lazily computed).
  const FlatExplanation<Op>& flatExplanation() {
    if (!flat_.has_value()) {
      flat_ = flattenTreeExplanation(trees_);
    }
    return flat_.value();
  }

  // Number of proof steps (tree form).
  std::size_t treeSize() const { return trees_.size(); }

 private:
  // Flatten a TreeExplanation into a FlatExplanation.
  static FlatExplanation<Op> flattenTreeExplanation(
      const TreeExplanation<Op>& trees) {
    FlatExplanation<Op> result;
    for (const auto& tree_ptr : trees) {
      result.push_back(flattenTreeTerm(*tree_ptr));
    }
    return result;
  }

  // Convert a single TreeTerm to a FlatTerm.
  static FlatTerm<Op> flattenTreeTerm(const TreeTerm<Op>& tree) {
    FlatTerm<Op> flat;
    flat.node = tree.node;
    flat.forward_rule = tree.forward_rule;
    flat.backward_rule = tree.backward_rule;

    for (std::size_t i = 0; i < tree.child_proofs.size(); ++i) {
      const auto& child_proof = tree.child_proofs[i];
      if (child_proof.empty()) {
        // No proof for this child.
        FlatTerm<Op> placeholder;
        flat.children.push_back(std::move(placeholder));
      } else {
        // Use the last step of the child proof as the representative.
        flat.children.push_back(flattenTreeTerm(*child_proof.back()));
      }
    }
    return flat;
  }

  TreeExplanation<Op> trees_;
  std::optional<FlatExplanation<Op>> flat_;
};

// ---------------------------------------------------------------------------
// Free functions for building TreeExplanation from the proof forest.
//
// These are used by EGraph::explainIdEquivalence() and operate on the
// proof forest (Explain<Op>) and the node storage.
// ---------------------------------------------------------------------------

namespace detail {

// Convert a single e-node Id to a TreeTerm (no rule annotation).
template <typename Op>
std::shared_ptr<TreeTerm<Op>> nodeToTreeTerm(
    Id id, const std::vector<ENode<Op>>& nodes) {
  auto term = std::make_shared<TreeTerm<Op>>();
  if (id.value < nodes.size()) {
    term->node = nodes[id.value];
  }
  return term;
}

// Forward declaration for mutual recursion.
template <typename Op, typename OpHash>
TreeExplanation<Op> explainEnodes(Id left, Id right,
                                  Explain<Op, OpHash>& explain,
                                  const std::vector<ENode<Op>>& nodes);

// Explain one step (one connection) in the proof.
template <typename Op, typename OpHash>
std::shared_ptr<TreeTerm<Op>> explainAdjacent(
    const Connection& conn, Explain<Op, OpHash>& explain,
    const std::vector<ENode<Op>>& nodes) {
  auto term = std::make_shared<TreeTerm<Op>>();

  // The "next" node in the connection is the node we're moving TO.
  Id target_id = conn.next;

  if (target_id.value < nodes.size()) {
    term->node = nodes[target_id.value];
  }

  if (conn.justification.isRule()) {
    if (conn.is_rewrite_forward) {
      term->forward_rule = conn.justification.rule_name;
    } else {
      term->backward_rule = conn.justification.rule_name;
    }
  } else {
    // Congruence: the two e-nodes have the same operator but different
    // children.  For each child position, recursively explain why the
    // children are equivalent.
    Id from_id = conn.current;
    Id to_id = conn.next;

    if (from_id.value < nodes.size() && to_id.value < nodes.size()) {
      const auto& from_node = nodes[from_id.value];
      const auto& to_node = nodes[to_id.value];

      if (from_node.children.size() == to_node.children.size()) {
        for (std::size_t i = 0; i < from_node.children.size(); ++i) {
          Id from_child = from_node.children[i];
          Id to_child = to_node.children[i];

          if (from_child != to_child) {
            // Recursively explain this child pair.
            auto child_explanation =
                explainEnodes<Op, OpHash>(from_child, to_child, explain, nodes);
            term->child_proofs.push_back(std::move(child_explanation));
          } else {
            // Children are the same id — trivial proof.
            TreeExplanation<Op> trivial;
            trivial.push_back(nodeToTreeTerm<Op>(from_child, nodes));
            term->child_proofs.push_back(std::move(trivial));
          }
        }
      }
    }
  }

  return term;
}

// Explain why two e-nodes are equivalent by walking the proof path.
template <typename Op, typename OpHash>
TreeExplanation<Op> explainEnodes(Id left, Id right,
                                  Explain<Op, OpHash>& explain,
                                  const std::vector<ENode<Op>>& nodes) {
  auto path = explain.getPathUnoptimized(left, right);

  TreeExplanation<Op> result;

  // First term: the starting e-node.
  result.push_back(nodeToTreeTerm<Op>(left, nodes));

  // For each connection, produce a proof step.
  for (const auto& conn : path) {
    result.push_back(explainAdjacent<Op, OpHash>(conn, explain, nodes));
  }

  return result;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// User-facing free functions for explaining equivalences.
//
// These are free functions (not EGraph members) because they depend on types
// defined in this header (Explanation, TreeTerm, etc.) which are only
// forward-declared in egraph.h.
// ---------------------------------------------------------------------------

// Explain why two e-class Ids are equivalent.
// Returns an Explanation containing the full proof.
// Throws std::logic_error if explanations are not enabled.
// Throws std::invalid_argument if the ids are not equivalent.
template <typename Op, typename Analysis, typename OpHash>
Explanation<Op> explainIdEquivalence(EGraph<Op, Analysis, OpHash>& egraph,
                                     Id left, Id right) {
  if (!egraph.explanationsEnabled()) {
    throw std::logic_error(
        "explanations are not enabled; call withExplanationsEnabled() first");
  }

  Id left_canon = egraph.find(left);
  Id right_canon = egraph.find(right);
  if (left_canon != right_canon) {
    throw std::invalid_argument(
        "cannot explain equivalence: ids are not equivalent");
  }

  auto trees = detail::explainEnodes<Op, OpHash>(left, right, egraph.explain(),
                                                 egraph.nodeStorage());

  return Explanation<Op>(std::move(trees));
}

// Explain why two RecExprs are equivalent.
// Looks up each expression in the e-graph, then explains the equivalence
// of the resulting Ids.
// Throws std::logic_error if explanations are not enabled.
// Throws std::invalid_argument if the expressions are not found or not
// equivalent.
template <typename Op, typename Analysis, typename OpHash>
Explanation<Op> explainEquivalence(EGraph<Op, Analysis, OpHash>& egraph,
                                   const RecExpr<Op>& left,
                                   const RecExpr<Op>& right) {
  if (!egraph.explanationsEnabled()) {
    throw std::logic_error(
        "explanations are not enabled; call withExplanationsEnabled() first");
  }

  Id left_id = egraph.lookupRecExpr(left);
  Id right_id = egraph.lookupRecExpr(right);

  return explainIdEquivalence<Op, Analysis, OpHash>(egraph, left_id, right_id);
}

}  // namespace egraph
}  // namespace nndeploy

#endif
