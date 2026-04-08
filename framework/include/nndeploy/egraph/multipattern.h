#ifndef _NNDEPLOY_EGRAPH_MULTIPATTERN_H_
#define _NNDEPLOY_EGRAPH_MULTIPATTERN_H_

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "nndeploy/egraph/egraph.h"
#include "nndeploy/egraph/pattern.h"
#include "nndeploy/egraph/searcher.h"
#include "nndeploy/egraph/subst.h"

namespace nndeploy {
namespace egraph {

// ---------------------------------------------------------------------------
// MultiPattern<Op>: a set of patterns that share variables.
//
// Mirrors egg's MultiPattern.
//
// A multi-pattern matches when ALL of its constituent patterns match
// simultaneously with consistent variable bindings.  The patterns may
// match in different e-classes.  Results are grouped by the first
// pattern's matched e-class.
//
// Example usage:
//   Pattern 0: (+ ?a ?b)   — matches in some e-class X
//   Pattern 1: (* ?a ?b)   — matches in some e-class Y
//   The multi-pattern finds substitutions where both patterns match
//   with the same bindings for ?a and ?b.
// ---------------------------------------------------------------------------

template <typename Op>
class MultiPattern {
 public:
  // Construct from a vector of patterns.  Requires at least 1 pattern.
  explicit MultiPattern(std::vector<Pattern<Op>> patterns)
      : patterns_(std::move(patterns)) {
    if (patterns_.empty()) {
      throw std::invalid_argument("MultiPattern requires at least one pattern");
    }
    collectVars();
  }

  // Accessors.
  const std::vector<Pattern<Op>>& patterns() const { return patterns_; }
  const std::vector<Var>& vars() const { return vars_; }
  std::size_t size() const { return patterns_.size(); }

  // ------------------------------------------------------------------
  // search: find all multi-matches across the entire egraph.
  //
  // Algorithm:
  //   1. Match pattern[0] across all e-classes.
  //   2. For each subsequent pattern[i], extend surviving substitutions
  //      by searching every e-class with the pre-bound variables.
  //   3. Results are grouped by pattern[0]'s matched e-class.
  // ------------------------------------------------------------------
  template <typename Analysis, typename OpHash>
  std::vector<SearchMatches<Op>> search(
      EGraph<Op, Analysis, OpHash>& egraph) const {
    // Step 1: Match the first pattern.
    std::vector<SearchMatches<Op>> results = patterns_[0].search(egraph);

    // Step 2: For each subsequent pattern, filter/extend substitutions.
    std::vector<Id> all_classes = egraph.roots();

    for (std::size_t p = 1; p < patterns_.size(); ++p) {
      const auto& pat = patterns_[p];

      for (auto& sm : results) {
        std::vector<Subst> extended;

        for (const auto& existing_subst : sm.substs) {
          // Try matching pattern[p] in every e-class with the existing
          // variable bindings.
          for (const auto& eclass_id : all_classes) {
            std::vector<Subst> candidates;
            candidates.push_back(existing_subst);

            auto matches =
                pat.searchEclassWithSubsts(egraph, eclass_id, candidates);

            for (auto& s : matches.substs) {
              extended.push_back(std::move(s));
            }
          }
        }

        sm.substs = std::move(extended);
      }

      // Remove entries with no surviving substitutions.
      results.erase(std::remove_if(results.begin(), results.end(),
                                   [](const SearchMatches<Op>& sm) {
                                     return sm.substs.empty();
                                   }),
                    results.end());

      if (results.empty()) break;
    }

    return results;
  }

  // ------------------------------------------------------------------
  // searchEclass: find multi-matches anchored at a specific e-class
  // for pattern[0].
  // ------------------------------------------------------------------
  template <typename Analysis, typename OpHash>
  SearchMatches<Op> searchEclass(EGraph<Op, Analysis, OpHash>& egraph,
                                 Id eclass_id) const {
    // Match the first pattern in this e-class.
    SearchMatches<Op> result = patterns_[0].searchEclass(egraph, eclass_id);

    if (result.substs.empty() || patterns_.size() <= 1) {
      return result;
    }

    // For each subsequent pattern, extend substitutions.
    std::vector<Id> all_classes = egraph.roots();

    for (std::size_t p = 1; p < patterns_.size(); ++p) {
      const auto& pat = patterns_[p];
      std::vector<Subst> extended;

      for (const auto& existing_subst : result.substs) {
        for (const auto& cls_id : all_classes) {
          std::vector<Subst> candidates;
          candidates.push_back(existing_subst);

          auto matches = pat.searchEclassWithSubsts(egraph, cls_id, candidates);

          for (auto& s : matches.substs) {
            extended.push_back(std::move(s));
          }
        }
      }

      result.substs = std::move(extended);
      if (result.substs.empty()) break;
    }

    return result;
  }

 private:
  void collectVars() {
    vars_.clear();
    for (const auto& pat : patterns_) {
      for (const auto& v : pat.vars()) {
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

  std::vector<Pattern<Op>> patterns_;
  std::vector<Var> vars_;
};

// ---------------------------------------------------------------------------
// MultiPatternSearcher<Op, Analysis, OpHash>: a Searcher backed by a
// MultiPattern<Op>.
//
// Wraps a MultiPattern and implements the Searcher interface so it can
// be used with Rewrite and Runner.
// ---------------------------------------------------------------------------

template <typename Op, typename Analysis = NoAnalysis,
          typename OpHash = std::hash<Op>>
class MultiPatternSearcher : public Searcher<Op, Analysis, OpHash> {
 public:
  using Graph = EGraph<Op, Analysis, OpHash>;

  explicit MultiPatternSearcher(MultiPattern<Op> mp) : mp_(std::move(mp)) {}

  std::vector<SearchMatches<Op>> search(Graph& egraph) const override {
    return mp_.search(egraph);
  }

  SearchMatches<Op> searchEclass(Graph& egraph, Id eclass_id) const override {
    return mp_.searchEclass(egraph, eclass_id);
  }

  std::vector<Var> vars() const override { return mp_.vars(); }

  const MultiPattern<Op>& multiPattern() const { return mp_; }

 private:
  MultiPattern<Op> mp_;
};

}  // namespace egraph
}  // namespace nndeploy

#endif
