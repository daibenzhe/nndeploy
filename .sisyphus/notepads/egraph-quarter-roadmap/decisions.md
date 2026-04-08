# Decisions

## [2026-04-08] Session Start

### Scope Decisions (from Metis + Momus planning)
- Primary persona: C++ API consumers inside nndeploy
- Breaking-change policy: no intentional public API breakage unless clearly justified and documented in README
- Parser expansion: deferred this quarter
- Incremental rebuild redesign: deferred this quarter
- Broad egg-parity chase: deferred this quarter

### Wave Structure
- Wave 1: T1, T2, T3, T4, T5 (all parallel, no blockers)
- Wave 2: T6, T7, T10 (after Wave 1)
- Wave 3: T8, T9, T14 (after Wave 2)
- Wave 4: T11, T12, T13 (after Wave 3)
- Wave FINAL: F1, F2, F3, F4 (after all)

## [2026-04-08] Task 10: Public API stability audit

### API stability decisions
- Treat the main adoption surface as `EGraph` add/merge/rebuild + `addExpr(RecExpr)`, `RecExpr`, `Pattern`/`PatternSearcher`, `Rewrite` + `Runner`, `Extractor`, and the explanation free functions.
- Keep `unionTrusted()` and `searchEclass` naming unchanged this quarter for compatibility; document the naming split instead of renaming public entry points.
- Mark low-level explanation internals (`Explain`, `ExplainNode`, `Connection`, `egraph.explain()`, `nodeStorage()`) as provisional rather than expanding support commitments around them.
- Mark `addExpr(const Node&, const std::vector<Node>&)` as a legacy convenience API and steer new callers to `addExpr(const RecExpr<Op>&)`.
