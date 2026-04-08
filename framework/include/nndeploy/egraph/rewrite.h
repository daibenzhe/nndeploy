#ifndef _NNDEPLOY_EGRAPH_REWRITE_H_
#define _NNDEPLOY_EGRAPH_REWRITE_H_

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "nndeploy/egraph/egraph.h"
#include "nndeploy/egraph/pattern.h"
#include "nndeploy/egraph/recexpr.h"
#include "nndeploy/egraph/searcher.h"
#include "nndeploy/egraph/subst.h"

namespace nndeploy {
namespace egraph {

// ---------------------------------------------------------------------------
// Rewrite<Op, Analysis, OpHash>: a rewrite rule backed by a Searcher and
// an Applier.
//
// There are two construction modes:
//
// 1. Pattern-only (backward compatible):
//      Rewrite(name, lhs_pattern, rhs_pattern)
//    This creates a PatternSearcher for the LHS and a PatternApplier for
//    the RHS.  Variable validation (every RHS var must appear in LHS) is
//    performed at construction time.
//
// 2. Generic (new):
//      Rewrite(name, searcher_ptr, applier_ptr)
//    This accepts arbitrary Searcher/Applier implementations.  No
//    automatic variable validation is performed.
//
// In both cases the Rewrite owns its Searcher and Applier via shared_ptr
// (shared_ptr allows Rewrite to be copyable, which is required because
// the Runner stores vectors of Rewrite and passes const-references around).
//
// The caller is responsible for calling egraph.rebuild() after applying
// rewrites (when using Rewrite directly; the Runner handles this
// automatically).
// ---------------------------------------------------------------------------

template <typename Op, typename Analysis = NoAnalysis,
          typename OpHash = std::hash<Op>>
class Rewrite {
 public:
  using Graph = EGraph<Op, Analysis, OpHash>;
  using SearcherType = Searcher<Op, Analysis, OpHash>;
  using ApplierType = Applier<Op, Analysis, OpHash>;

  // -----------------------------------------------------------------------
  // Pattern-only constructor (backward compatible).
  // -----------------------------------------------------------------------
  Rewrite(std::string name, Pattern<Op> lhs, Pattern<Op> rhs)
      : name_(std::move(name)) {
    // Validate RHS variables are bound by LHS.
    validateRhsVars(lhs, rhs);

    searcher_ =
        std::make_shared<PatternSearcher<Op, Analysis, OpHash>>(std::move(lhs));
    applier_ = std::make_shared<PatternApplier<Op, Analysis, OpHash>>(
        std::move(rhs), name_);
  }

  // -----------------------------------------------------------------------
  // Generic constructor: takes arbitrary Searcher and Applier.
  // -----------------------------------------------------------------------
  Rewrite(std::string name, std::shared_ptr<SearcherType> searcher,
          std::shared_ptr<ApplierType> applier)
      : name_(std::move(name)),
        searcher_(std::move(searcher)),
        applier_(std::move(applier)) {
    if (!searcher_) {
      throw std::invalid_argument("Rewrite '" + name_ +
                                  "': searcher must not be null");
    }
    if (!applier_) {
      throw std::invalid_argument("Rewrite '" + name_ +
                                  "': applier must not be null");
    }
  }

  const std::string& name() const { return name_; }

  // Access the underlying searcher/applier.
  const SearcherType& searcher() const { return *searcher_; }
  const ApplierType& applier() const { return *applier_; }

  // Convenience accessors for pattern-based rewrites.
  // Returns nullptr if this rewrite does not use a PatternSearcher/Applier.
  const PatternSearcher<Op, Analysis, OpHash>* patternSearcher() const {
    return dynamic_cast<const PatternSearcher<Op, Analysis, OpHash>*>(
        searcher_.get());
  }
  const PatternApplier<Op, Analysis, OpHash>* patternApplier() const {
    return dynamic_cast<const PatternApplier<Op, Analysis, OpHash>*>(
        applier_.get());
  }

  // Backward-compatible accessors for the LHS/RHS patterns.
  // These throw if the rewrite is not pattern-based.
  const Pattern<Op>& lhs() const {
    auto* ps = patternSearcher();
    if (!ps) {
      throw std::logic_error("Rewrite '" + name_ +
                             "' does not use a pattern searcher");
    }
    return ps->pattern();
  }
  const Pattern<Op>& rhs() const {
    auto* pa = patternApplier();
    if (!pa) {
      throw std::logic_error("Rewrite '" + name_ +
                             "' does not use a pattern applier");
    }
    return pa->pattern();
  }

  // Search: find all matches in the egraph.
  std::vector<SearchMatches<Op>> search(Graph& egraph) const {
    return searcher_->search(egraph);
  }

  // Apply: for each match, apply the rewrite.  Returns the number of
  // merges performed.
  std::size_t apply(Graph& egraph,
                    const std::vector<SearchMatches<Op>>& matches) const {
    return applier_->applyMatches(egraph, matches);
  }

  // Convenience: search + apply in one call.  Returns number of merges.
  std::size_t run(Graph& egraph) const {
    auto matches = search(egraph);
    return apply(egraph, matches);
  }

 private:
  // Validate that every variable used in the RHS also appears in the LHS.
  static void validateRhsVars(const Pattern<Op>& lhs, const Pattern<Op>& rhs) {
    const auto& lhs_vars = lhs.vars();
    for (const auto& rhs_var : rhs.vars()) {
      bool found = false;
      for (const auto& lv : lhs_vars) {
        if (lv == rhs_var) {
          found = true;
          break;
        }
      }
      if (!found) {
        throw std::invalid_argument("rewrite RHS variable " +
                                    rhs_var.toString() +
                                    " is not bound by the LHS");
      }
    }
  }

  std::string name_;
  std::shared_ptr<SearcherType> searcher_;
  std::shared_ptr<ApplierType> applier_;
};

}  // namespace egraph
}  // namespace nndeploy

#endif
