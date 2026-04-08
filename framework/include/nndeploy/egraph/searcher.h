#ifndef _NNDEPLOY_EGRAPH_SEARCHER_H_
#define _NNDEPLOY_EGRAPH_SEARCHER_H_

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "nndeploy/egraph/egraph.h"
#include "nndeploy/egraph/pattern.h"
#include "nndeploy/egraph/recexpr.h"
#include "nndeploy/egraph/subst.h"

namespace nndeploy {
namespace egraph {

// ---------------------------------------------------------------------------
// Searcher<Op, Analysis, OpHash>: abstract interface for the search side
// of a rewrite rule.
//
// Mirrors egg's Searcher trait.
//
// A searcher finds substitutions in an e-graph that match some condition.
// The canonical implementation is PatternSearcher (pattern matching), but
// users can implement custom searchers for e.g. conditional rewrites,
// analysis-guided matching, etc.
// ---------------------------------------------------------------------------

template <typename Op, typename Analysis = NoAnalysis,
          typename OpHash = std::hash<Op>>
class Searcher {
 public:
  using Graph = EGraph<Op, Analysis, OpHash>;

  virtual ~Searcher() = default;

  // Search the entire e-graph and return matches grouped by e-class.
  virtual std::vector<SearchMatches<Op>> search(Graph& egraph) const = 0;

  // Search a single e-class.  Default: delegates to search() and filters.
  virtual SearchMatches<Op> searchEclass(Graph& egraph, Id eclass_id) const {
    auto all = search(egraph);
    Id canonical = egraph.find(eclass_id);
    for (auto& sm : all) {
      if (egraph.find(sm.eclass) == canonical) {
        return std::move(sm);
      }
    }
    SearchMatches<Op> empty;
    empty.eclass = canonical;
    return empty;
  }

  // Return the pattern variables used by this searcher.
  // For non-pattern searchers this may return an empty vector.
  virtual std::vector<Var> vars() const { return {}; }
};

// ---------------------------------------------------------------------------
// Applier<Op, Analysis, OpHash>: abstract interface for the apply side
// of a rewrite rule.
//
// Mirrors egg's Applier trait.
//
// An applier uses substitutions produced by a searcher to mutate the
// e-graph (typically by adding nodes and merging e-classes).
// ---------------------------------------------------------------------------

template <typename Op, typename Analysis = NoAnalysis,
          typename OpHash = std::hash<Op>>
class Applier {
 public:
  using Graph = EGraph<Op, Analysis, OpHash>;

  virtual ~Applier() = default;

  // Apply a single substitution to a matched e-class.
  // Returns the list of new Ids added or merged by this application.
  virtual std::vector<Id> applyOne(Graph& egraph, Id eclass,
                                   const Subst& subst) const = 0;

  // Apply all matches.  Returns the total number of merges performed.
  // Default implementation iterates over matches and calls applyOne().
  virtual std::size_t applyMatches(
      Graph& egraph, const std::vector<SearchMatches<Op>>& matches) const {
    std::size_t num_merges = 0;
    for (const auto& sm : matches) {
      for (const auto& subst : sm.substs) {
        auto ids = applyOne(egraph, sm.eclass, subst);
        num_merges += ids.size();
      }
    }
    return num_merges;
  }

  // Return the pattern variables used by this applier.
  // For non-pattern appliers this may return an empty vector.
  virtual std::vector<Var> vars() const { return {}; }
};

// ---------------------------------------------------------------------------
// PatternSearcher<Op, Analysis, OpHash>: a Searcher backed by a Pattern<Op>.
//
// This is the most common searcher: it wraps a Pattern and uses the
// existing pattern-matching engine.
// ---------------------------------------------------------------------------

template <typename Op, typename Analysis = NoAnalysis,
          typename OpHash = std::hash<Op>>
class PatternSearcher : public Searcher<Op, Analysis, OpHash> {
 public:
  using Graph = EGraph<Op, Analysis, OpHash>;

  explicit PatternSearcher(Pattern<Op> pattern)
      : pattern_(std::move(pattern)) {}

  std::vector<SearchMatches<Op>> search(Graph& egraph) const override {
    return pattern_.search(egraph);
  }

  SearchMatches<Op> searchEclass(Graph& egraph, Id eclass_id) const override {
    return pattern_.searchEclass(egraph, eclass_id);
  }

  std::vector<Var> vars() const override { return pattern_.vars(); }

  const Pattern<Op>& pattern() const { return pattern_; }

 private:
  Pattern<Op> pattern_;
};

// ---------------------------------------------------------------------------
// PatternApplier<Op, Analysis, OpHash>: an Applier backed by a Pattern<Op>.
//
// Given a substitution, it instantiates the RHS pattern bottom-up in the
// e-graph and merges the result into the matched e-class.
// ---------------------------------------------------------------------------

template <typename Op, typename Analysis = NoAnalysis,
          typename OpHash = std::hash<Op>>
class PatternApplier : public Applier<Op, Analysis, OpHash> {
 public:
  using Graph = EGraph<Op, Analysis, OpHash>;

  explicit PatternApplier(Pattern<Op> pattern, std::string rule_name = "")
      : pattern_(std::move(pattern)), rule_name_(std::move(rule_name)) {}

  std::vector<Id> applyOne(Graph& egraph, Id eclass,
                           const Subst& subst) const override {
    Id rhs_id = instantiate(egraph, subst);
    Id lhs_id = egraph.find(eclass);
    if (egraph.find(rhs_id) != lhs_id) {
      if (egraph.explanationsEnabled() && !rule_name_.empty()) {
        egraph.unionTrusted(lhs_id, rhs_id, rule_name_);
      } else {
        egraph.merge(lhs_id, rhs_id);
      }
      return {egraph.find(lhs_id)};
    }
    return {};
  }

  std::vector<Var> vars() const override { return pattern_.vars(); }

  const Pattern<Op>& pattern() const { return pattern_; }

 private:
  // Instantiate the pattern using the given substitution, adding the
  // resulting expression bottom-up into the egraph.  Returns the Id of the
  // root of the instantiated expression.
  Id instantiate(Graph& egraph, const Subst& subst) const {
    const auto& ast = pattern_.ast();
    std::vector<Id> id_map;
    id_map.reserve(ast.size());

    for (std::size_t i = 0; i < ast.size(); ++i) {
      const auto& wrapper = ast.at(i);
      const auto& payload = wrapper.op;

      if (payload.isVar()) {
        // Look up the variable binding.
        const Id* bound = subst.get(payload.var());
        if (bound == nullptr) {
          throw std::out_of_range("RHS variable " + payload.var().toString() +
                                  " not found in substitution");
        }
        id_map.push_back(egraph.find(*bound));
      } else {
        // ENode: build a concrete node with remapped children.
        const auto& pat_enode = payload.enode();
        ENode<Op> resolved;
        resolved.op = pat_enode.op;
        resolved.children.reserve(wrapper.children.size());
        for (const auto& child_ref : wrapper.children) {
          if (!child_ref.valid() || child_ref.value >= id_map.size()) {
            throw std::out_of_range("RHS pattern child index out of range");
          }
          resolved.children.push_back(id_map[child_ref.value]);
        }
        id_map.push_back(egraph.add(resolved));
      }
    }

    if (id_map.empty()) {
      throw std::out_of_range("cannot instantiate empty RHS pattern");
    }
    return id_map.back();
  }

  Pattern<Op> pattern_;
  std::string rule_name_;
};

}  // namespace egraph
}  // namespace nndeploy

#endif
