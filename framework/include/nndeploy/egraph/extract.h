#ifndef _NNDEPLOY_EGRAPH_EXTRACT_H_
#define _NNDEPLOY_EGRAPH_EXTRACT_H_

#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "nndeploy/egraph/egraph.h"
#include "nndeploy/egraph/recexpr.h"

namespace nndeploy {
namespace egraph {

// ---------------------------------------------------------------------------
// CostFunction concept (duck-typed):
//
//   struct MyCostFn {
//     using Cost = ...;  // must support operator<, copy
//     Cost cost(const ENode<Op>& enode, std::function<Cost(Id)> costs) const;
//   };
//
// The cost function is called with an enode and a lookup that returns the
// already-computed best cost for each child e-class Id.  The function must
// be monotonic: the returned cost must be >= every child cost.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// AstSize: counts total AST nodes.  Cost type is std::size_t.
// Mirrors egg::AstSize.
// ---------------------------------------------------------------------------

struct AstSize {
  using Cost = std::size_t;

  template <typename Op>
  Cost cost(const ENode<Op>& enode, std::function<Cost(Id)> child_costs) const {
    Cost total = 1;
    for (const auto& child : enode.children) {
      Cost c = child_costs(child);
      // Saturating add to avoid overflow on pathological cases.
      if (total > std::numeric_limits<Cost>::max() - c) {
        return std::numeric_limits<Cost>::max();
      }
      total += c;
    }
    return total;
  }
};

// ---------------------------------------------------------------------------
// AstDepth: counts maximum AST depth.  Cost type is std::size_t.
// Mirrors egg::AstDepth.
// ---------------------------------------------------------------------------

struct AstDepth {
  using Cost = std::size_t;

  template <typename Op>
  Cost cost(const ENode<Op>& enode, std::function<Cost(Id)> child_costs) const {
    Cost max_child = 0;
    for (const auto& child : enode.children) {
      Cost c = child_costs(child);
      if (c > max_child) {
        max_child = c;
      }
    }
    return 1 + max_child;
  }
};

// ---------------------------------------------------------------------------
// Extractor<Op, Analysis, CostFn, OpHash>
//
// Greedy bottom-up extractor that finds the cheapest RecExpr representable
// in each e-class.
//
// Construction runs the full fixed-point cost computation.
// findBest(id) reconstructs the best RecExpr from the cached results.
//
// Throws std::runtime_error if asked to extract from an e-class that has
// no finite acyclic representative (cycle-only classes).
// Throws std::out_of_range for invalid or unknown ids.
// ---------------------------------------------------------------------------

template <typename Op, typename Analysis = NoAnalysis,
          typename CostFn = AstSize, typename OpHash = std::hash<Op>>
class Extractor {
 public:
  using Graph = EGraph<Op, Analysis, OpHash>;
  using Node = ENode<Op>;
  using Cost = typename CostFn::Cost;

  // Construct the extractor and compute best costs for all reachable classes.
  // The egraph should be in a clean state (rebuild() called after merges).
  Extractor(Graph& egraph, CostFn cost_fn = CostFn())
      : egraph_(egraph), cost_fn_(std::move(cost_fn)) {
    findCosts();
  }

  // Find the cheapest RecExpr rooted at the given e-class.
  // Returns (cost, expr).
  std::pair<Cost, RecExpr<Op>> findBest(Id eclass) {
    Id canonical = egraph_.find(eclass);
    assertHasCost(canonical);
    RecExpr<Op> expr;
    std::unordered_map<std::size_t, Id> built;  // canonical id -> RecExpr id
    buildRecExpr(canonical, expr, built);
    Cost c = costs_.at(canonical.value).first;
    return {c, std::move(expr)};
  }

  // Find the cost of the best term in the given e-class.
  Cost findBestCost(Id eclass) {
    Id canonical = egraph_.find(eclass);
    assertHasCost(canonical);
    return costs_.at(canonical.value).first;
  }

  // Find the best (cheapest) e-node chosen for the given e-class.
  const Node& findBestNode(Id eclass) {
    Id canonical = egraph_.find(eclass);
    assertHasCost(canonical);
    return costs_.at(canonical.value).second;
  }

 private:
  // Fixed-point cost discovery.
  // Iterates until no class improves.
  void findCosts() {
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto& root_id : egraph_.roots()) {
        const auto* eclass = egraph_.getEClass(root_id);
        if (eclass == nullptr) continue;

        auto pass = makePass(*eclass);
        if (!pass.first) continue;  // no computable cost this round

        auto it = costs_.find(root_id.value);
        if (it == costs_.end()) {
          costs_.emplace(root_id.value,
                         std::make_pair(pass.second.first, pass.second.second));
          changed = true;
        } else if (pass.second.first < it->second.first) {
          it->second = std::move(pass.second);
          changed = true;
        }
      }
    }
  }

  // Evaluate all enodes in a class, return the cheapest one whose children
  // all have known costs.
  // Returns {true, {cost, node}} on success, {false, {}} if no node is
  // computable.
  std::pair<bool, std::pair<Cost, Node>> makePass(
      const EClass<Op, Analysis>& eclass) {
    bool found_any = false;
    Cost best_cost{};
    Node best_node{};

    for (const auto& node : eclass.nodes) {
      auto node_cost = nodeTotalCost(node);
      if (!node_cost.first) continue;  // some child has no cost yet

      if (!found_any || node_cost.second < best_cost) {
        best_cost = node_cost.second;
        best_node = node;
        found_any = true;
      }
    }

    if (!found_any) {
      return {false, {}};
    }
    return {true, {best_cost, best_node}};
  }

  // Compute the total cost for a single enode.
  // Returns {true, cost} if all children have known costs, {false, _}
  // otherwise.
  std::pair<bool, Cost> nodeTotalCost(const Node& node) {
    // Check that all children have costs.
    for (const auto& child : node.children) {
      Id child_canonical = egraph_.find(child);
      if (costs_.find(child_canonical.value) == costs_.end()) {
        return {false, Cost{}};
      }
    }

    // All children have costs — compute this node's cost.
    auto child_cost_fn = [this](Id child_id) -> Cost {
      Id canonical = egraph_.find(child_id);
      return costs_.at(canonical.value).first;
    };

    Cost c = cost_fn_.cost(node, std::function<Cost(Id)>(child_cost_fn));
    return {true, c};
  }

  // Reconstruct a RecExpr bottom-up from cached best nodes.
  // Uses `built` to deduplicate: if a canonical class was already emitted,
  // reuse its RecExpr Id.
  Id buildRecExpr(Id canonical, RecExpr<Op>& expr,
                  std::unordered_map<std::size_t, Id>& built) {
    auto it = built.find(canonical.value);
    if (it != built.end()) {
      return it->second;
    }

    assertHasCost(canonical);
    const Node& best = costs_.at(canonical.value).second;

    // Recursively build children first.
    Node resolved;
    resolved.op = best.op;
    resolved.children.reserve(best.children.size());
    for (const auto& child : best.children) {
      Id child_canonical = egraph_.find(child);
      Id child_recexpr_id = buildRecExpr(child_canonical, expr, built);
      resolved.children.push_back(child_recexpr_id);
    }

    Id recexpr_id = expr.add(resolved);
    built.emplace(canonical.value, recexpr_id);
    return recexpr_id;
  }

  void assertHasCost(Id canonical) {
    if (costs_.find(canonical.value) == costs_.end()) {
      throw std::runtime_error("cannot extract from e-class " +
                               std::to_string(canonical.value) +
                               ": no finite acyclic term found");
    }
  }

  Graph& egraph_;
  CostFn cost_fn_;
  // Map from canonical class id value -> (best cost, best node).
  std::unordered_map<std::size_t, std::pair<Cost, Node>> costs_;
};

}  // namespace egraph
}  // namespace nndeploy

#endif
