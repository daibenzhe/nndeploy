# Learnings

## [2026-04-08] Session Start

### Build Setup
- Build command: `cmake -DENABLE_NNDEPLOY_TEST=ON -DENABLE_NNDEPLOY_OPENCV=OFF -B build .` from project root
- Then: `make egraph_test -j4`
- Test binary: `./build/egraph_test`
- Baseline: 96 tests, 9 suites, all passing

### Code Layout
- All egraph headers: `framework/include/nndeploy/egraph/`
- Test file: `test/source/nndeploy/egraph/egraph_test.cc` (~2221 lines, single file)
- README spec: `framework/include/nndeploy/egraph/README.md`

### Task 3 Cleanup Notes
- Shared local builders in `egraph_test.cc` now cover repeated binary graphs, `RecExpr` construction, and common pattern AST setup.
- `PatternTest`, `MatcherTest`, and `SearcherApplierTest` all reuse the same helpers, which keeps the suite readable without changing test names or behavior.
- Verification stayed at 96 tests / 9 suites passing after the cleanup.

### Key Headers
- `egraph.h` — Core e-graph, UnionFind, EGraph<Op,Analysis>, Justification, Explain types
- `explain.h` — TreeTerm, FlatTerm, Explanation, explainIdEquivalence, explainEquivalence
- `recexpr.h` — RecExpr<Op> with topological validation
- `subst.h` — Var and Subst types
- `pattern.h` — ENodeOrVar, PatternAst, Pattern, searchEclassWithSubsts
- `searcher.h` — Searcher, Applier, PatternSearcher, PatternApplier
- `rewrite.h` — Rewrite
- `extract.h` — AstSize, AstDepth, Extractor
- `runner.h` — StopReason, Iteration, Report, schedulers, Runner
- `multipattern.h` — MultiPattern, MultiPatternSearcher

### [2026-04-08] Task 7: Pattern ergonomics
- Added parser-free pattern helpers in `pattern.h`: `makePatternVar`, `makePatternNode`, and `PatternBuilder`.
- `PatternBuilder` keeps pattern authoring topological and explicit; repeated variable semantics still come from reusing the same variable name.
- Variable-with-children inputs are still rejected at build time; the new ergonomic path does not weaken validation.
- Builder round-trip coverage compares semantic AST shape and search behavior, not raw payload metadata.

### Test Suite Names (existing)
- EGraphTest, PatternTest, MatcherTest, RewriteTest, ExtractorTest, RunnerTest, SearcherApplierTest, MultiPatternTest, ExplanationTest

### Test Suite Names (expected by plan for new tests)
- `ErrorContractTest.*` — Task 2
- `RecExprErgonomicsTest.*` — Task 6
- `PatternErgonomicsTest.*` — Task 7
- `InteropWorkflowTest.*` — Task 8
- `CookbookWorkflowTest.*` or `SmokeTest.*` — Task 9
- `ApiStabilitySmokeTest.*` — Task 10
- `SmokeTest.EndToEndAdoptionFlow` — Task 11
- `EdgeCaseResilienceTest.*` — Task 12

## [2026-04-08] Task 5: Runner/Report Diagnostic Hardening

### What was done
- Added 9 new `RunnerTest.*` tests covering:
  - `SaturatedStopReasonHasCorrectKindAndLimitValue` — verifies `limit_value=0` and `message=""` for Saturated
  - `IterationLimitLimitValueMatchesConfig` — verifies `limit_value == configured_limit`
  - `NodeLimitLimitValueIsCurrentNodeCount` — verifies `limit_value == egraph.memoSize()` at violation
  - `ReportIterationsConsistentWithIterationsVec` — verifies report fields mirror runner state
  - `ReportTimingFieldsAreNonNegativeAfterIterLimit` — timing invariant after non-saturation stop
  - `RuleApplicationVisibilityNamedRulesTraceable` — multiple named rules visible in `applied` map
  - `RuleApplicationVisibilitySingleRuleCountsPositive` — applied entries are always >0
  - `StopReasonKindMatchesRunnerAndReport` — triple consistency check (runner, last iter, report)
  - `ReportToStringContainsAllFields` — ensures `toString()` output is log-parseable
- RunnerTest count: 17 → 26

### runner.h changes
- Added default initializers to `Iteration::egraph_nodes` and `Iteration::egraph_classes` (were uninitialized before)
- Expanded `Iteration` block comment with diagnostic guide:
  - `applied` only contains rules with >0 merges
  - How to compute per-rule totals
  - `has_stop_reason` only on the last iteration

### README changes (runner section)
- `StopReason`: documented `limit_value` and `elapsed_seconds` semantics per variant
- `Iteration`: documented `applied` map filtering behavior, how to sum totals
- `Report`: added annotated example output, added interpretation guidance
- `SimpleScheduler`: added when-to-use note
- `BackoffScheduler`: added when-to-use and `doNotBan` guidance
- `Runner`: added "Debugging a run" section with actionable diagnostic steps

### Pre-existing failures
- `ErrorContractTest.ForeignIdInMergeThrows` was failing before Task 5 and remains so (not introduced by this task)

## [2026-04-08] Task 4: Explanation usability hardening

### What was done
- Added 14 new ExplanationTest.* cases (17 → 31 total), all passing
- Fixed misleading `lookupRecExpr` comment: it does NOT require explanations to be enabled; it falls back to canonical memo
- Added `## Explanation usage guide` section to README covering: how to enable, free function API, throw table, code examples, constraints
- Updated README test coverage list with all new cases
- Saved evidence: `.sisyphus/evidence/task-4-explanations.txt`, `.sisyphus/evidence/task-4-doc-sync.txt`

### New test coverage added
- DirectMergeRuleNamePresentInProof, DirectMergeReverseDirectionHasRuleAnnotation
- CongruenceChildProofsHaveContent
- FourStepTransitiveProof (a→b→c→d, all three rules must appear)
- AlternateRewriteFirstRuleStillVisible
- FlatProofPreservesRuleAnnotations, FlatTermRemoveRewritesClearsAnnotations
- ExplainEquivalenceNotFoundThrows (new error path)
- NonEquivalentPairsThrowInvalidArgument
- FlatTermDirectionalHelpers, FlatTermRecursiveForwardCheck
- TrivialSelfExplanationFlatForm, FlatTermGetRecExprLeafNode
- ExplainAfterRunnerSaturationWorks

### Pre-existing failure
- ErrorContractTest.ForeignIdInMergeThrows — was already failing before Task 4 changes (EGraph.merge() does not validate foreign Ids). Not in our scope.

## [2026-04-08] Task 6: RecExpr ergonomics

### What was added
- Introduced `RecExprBuilder<Op>` in `recexpr.h` with explicit `addLeaf()` and `addNode()` helpers.
- Kept the same validation path as `RecExpr::add()` so forward/foreign child ids still fail fast.
- Documented the builder in `framework/include/nndeploy/egraph/README.md` with a short cookbook example.
- Added focused `RecExprErgonomicsTest.*` coverage for topological construction, invalid ids, and round-tripping against a manual `RecExpr`.

### Verification
- Targeted filter passed: `RecExprErgonomicsTest.*:EGraphTest.RecExpr*:ExtractorTest.*`
- Build target remained green with `make egraph_test -j4`.

## [2026-04-08] Task 10: Public API stability audit

### What was added
- Added a `Public API stability status` section to the egraph README that separates stable adoption entry points from provisional hooks.
- Documented compatibility notes for low-level explanation internals, the legacy `addExpr(Node, subexpressions)` helper, global rebuild implementation details, and the current throwing behavior of `contains(Id)`.
- Added 6 `ApiStabilitySmokeTest.*` tests covering the intended public entry points directly: EGraph add/merge/rebuild, RecExpr + addExpr, PatternSearcher search, Runner + Rewrite, Extractor, and explanation free functions.
- Added a brief header comment on `EGraph::contains()` to make its throwing invalid-id semantics explicit.

### Verification
- `make egraph_test -j4`
- `./build/egraph_test --gtest_filter=ApiStabilitySmokeTest.*`
- `./build/egraph_test`
- LSP diagnostics clean on `framework/include/nndeploy/egraph/egraph.h` and `test/source/nndeploy/egraph/egraph_test.cc`

### Evidence
- `.sisyphus/evidence/task-10-stability-audit.txt`
- `.sisyphus/evidence/task-10-full-suite.txt`

## [2026-04-08] Task 8: Interop regression pack

### What was added
- Added `InteropWorkflowTest` suite (4 tests) to `test/source/nndeploy/egraph/egraph_test.cc`:
  - `RewriteThenRebuildThenExtract` — verifies rewrite application + rebuild + cost-based extraction round-trips correctly.
  - `RewriteThenRebuildThenExplain` — verifies that after a `Rewrite.run()`, explanation includes the rule name in the proof steps.
  - `MultiStepRewriteExtractExplain` — chains two rewrite rules, then extracts and explains end-to-end.
  - `RunnerSaturationExtractExplain` — saturates via `Runner`, then extracts the best expression and verifies an explanation exists.

### Critical interop bug found and fixed
**File**: `framework/include/nndeploy/egraph/searcher.h` — `PatternApplier`

**Root cause**: `PatternApplier::applyOne()` was always calling `egraph.merge(lhs_id, rhs_id)` (plain merge), even when explanations were enabled. This meant rewrite rules applied via `Rewrite.run()` never recorded the rule name in the proof forest. The flat explanation had no rule annotations after a `Rewrite` application.

**Fix**: Added `rule_name_` field to `PatternApplier` (default empty string for backward compat). Updated `applyOne()` to call `egraph.unionTrusted(lhs_id, rhs_id, rule_name_)` when `egraph.explanationsEnabled() && !rule_name_.empty()`, otherwise falls back to `egraph.merge()`.

**File**: `framework/include/nndeploy/egraph/rewrite.h`

**Fix**: Pattern constructor now passes `name_` to `PatternApplier` so the rule name propagates into the proof forest.

### FlatTerm API notes
- `FlatTerm.forward_rule` and `backward_rule` are struct fields (not methods).
- Use `hasRewriteForward()` / `hasRewriteBackward()` for recursive rule-presence checks.
- Rule name checking requires recursive traversal of `children`.

### Verification
- `make egraph_test -j4` — clean build
- Focused filter: all 49 tests in `InteropWorkflowTest.*:RewriteTest.*:ExtractorTest.*:ExplanationTest.*` pass
- Full suite: **164 tests, 0 failures** (160 pre-existing + 4 new)

### Evidence
- `.sisyphus/evidence/task-8-interop.txt`
- `.sisyphus/evidence/task-8-full-suite.txt`

## Task 9: Cookbook workflow docs + smoke-backed examples

- **Cookbook Patterns**: Established a pattern for README documentation where each code snippet is explicitly mapped to a named gtest case. This ensures documentation stays "fresh" and executable.
- **Verification Workflow**: Integrated 6 new smoke tests in `CookbookWorkflowTest` within `egraph_test.cc` to back the new "Cookbook Workflows" section.
- **API Nuance**: Confirmed `Var` comparison in tests should use `Var::fromString("?x")` rather than `Var.name()` (which doesn't exist in the current public contract).
- **Explanation Setup**: Re-verified that `withExplanationsEnabled()` must be the first call on a fresh e-graph to avoid `std::logic_error`.

## [2026-04-08] Task 14: Next-quarter decision package

### Decision evidence captured
- Added a forward-looking `Next-Quarter Decision Package` section to the egraph README; it explicitly states that neither parser work nor incremental rebuild is being implemented this quarter.
- Parser-first criteria are grounded in quarter ergonomics evidence: `RecExprBuilder` and `PatternBuilder` reduced bookkeeping, but cookbook and API-smoke-backed workflows still require explicit topological C++ node construction.
- Incremental-rebuild-first criteria are grounded in current architecture: `EGraph::rebuild()` remains global, `dirty_roots_` is only a trigger, and `Runner::run()` still performs rebuild before and during every saturation run.
- Tie-breaker recommendation stays usability-first unless future smoke/resilience data shows rebuild cost, `NodeLimit`, `TimeLimit`, or `rebuild_time` dominating real workloads.

## [2026-04-08] Task 12: Edge-case resilience pack

### What was added
- Added `EdgeCaseResilienceTest` suite (5 tests) to `test/source/nndeploy/egraph/egraph_test.cc`:
  - `AlreadyEqualMergeIsNoOp` — `merge(id, id)` is a no-op; class count and memo size unchanged.
  - `NoMatchPathReturnsZeroMerges` — rewrite with no applicable LHS matches returns 0 merges; graph unchanged.
  - `CycleResilientExtractionPicksLeaf` — after `merge(a, f(a))` + rebuild, class contains both `a` (leaf) and `f(self)`; Extractor picks `a` (cost 1) ignoring the cyclic node.
  - `ExplanationMisuseThrowsLogicError` — `explainIdEquivalence`, `explainEquivalence`, and `egraph.explain()` all throw `std::logic_error` when called without `withExplanationsEnabled()`.
  - `ZeroRewritesSaturatesImmediately` — `Runner` with empty rules vector reaches `Saturated` in exactly 1 iteration with empty `applied` map.

### Key findings
- **No bugs exposed**: all 5 edge cases were already handled correctly by the existing implementation. The tests confirm behavior, not fix regressions.
- **Cycle-only class insight**: a truly cycle-only e-class (containing no leaf nodes) cannot be created through normal `EGraph` API calls, because `add()` requires all child Ids to already exist. The `assertHasCost()` `std::runtime_error` in `extract.h` is therefore a defensive guard against externally corrupted or directly mutated graph state. The `CycleResilientExtractionPicksLeaf` test documents the realistic scenario where merge+rebuild creates a self-referential node but the leaf coexists in the class.
- **Already-equal merge**: both `mergeInternal` and `mergeWithJustification` short-circuit when `lhs == rhs` (canonical equality), verified by checking class count and memo size are unchanged.
- **Saturation with zero rules**: the saturation check fires immediately in the first iteration because `applied.empty()`, `canStop()` returns true, hooks are unchanged, and graph size is unchanged. Exactly 1 iteration is recorded.

### Test suite count
- 171 → 176 tests, 16 → 17 suites, all passing.

### Evidence files
- `.sisyphus/evidence/task-12-edge-cases.txt` — 71 tests (filtered), all PASSED
- `.sisyphus/evidence/task-12-full-suite.txt` — 176 tests (full suite), all PASSED

## [2026-04-08] Task 11 - End-to-end adoption smoke scenario

### What was done
- Added `SmokeTest.EndToEndAdoptionFlow` to `test/source/nndeploy/egraph/egraph_test.cc` (appended after `CookbookWorkflowTest`).
- Test exercises: RecExprBuilder → runner.egraph.withExplanationsEnabled() → runner.addExpr(expr) → runner.run(rules) → Extractor::findBest → explainIdEquivalence.
- Updated `framework/include/nndeploy/egraph/README.md`: added one sentence pointing to `SmokeTest.EndToEndAdoptionFlow` under "Supported This Quarter".

### Key patterns confirmed
- `runner.egraph.withExplanationsEnabled()` must be called BEFORE `runner.addExpr()`, not after.
- `runner.egraph.lookupRecExpr(just_a)` is needed to get the Id for `a` after saturation so explainIdEquivalence can compare root ≡ a.
- `explainIdEquivalence` returns treeSize() >= 2 after a rewrite step (one node for original, one for rewritten form).
- The new suite adds 1 test: total goes from 170 → 171 tests, 15 → 16 suites.

### Evidence files
- `.sisyphus/evidence/task-11-smoke.txt` — filtered run, 1 test, PASSED
- `.sisyphus/evidence/task-11-full-suite.txt` — full suite, 171 tests, all PASSED
