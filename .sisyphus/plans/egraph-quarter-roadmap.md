# E-graph Quarter Roadmap — Production Usability First

## TL;DR

> **Quick Summary**: Turn the new `nndeploy::egraph` subsystem from “feature-complete enough for internal experimentation” into a reliable, documented, regression-hardened C++ subsystem that other `nndeploy` code can adopt safely this quarter.
>
> **Deliverables**:
> - Hardened public C++ usability contract and non-goals
> - Regression-focused API, explanation, and runner diagnostics improvements
> - Practical cookbook-style examples and smoke-tested workflows
> - Release-readiness checklist for quarter-close adoption
>
> **Estimated Effort**: Large
> **Parallel Execution**: YES — 4 implementation waves + final verification wave
> **Critical Path**: Task 1 → Task 4 → Task 7 → Task 11 → Final Verification

---

## Context

### Original Request
Create a long-period roadmap for the e-graph work.

### Interview Summary
**Key Discussions**:
- Horizon: 1 quarter
- Priority: production usability over parity-first or performance-first work
- Scope: usability core only
- Verification: TDD where practical, with agent-executed QA for all implementation tasks

**Research Findings**:
- Current implemented baseline includes core e-graph, pattern matching, rewrite system, extractor, runner/schedulers, multi-pattern support, and explanations/proof tracing.
- `framework/include/nndeploy/egraph/README.md` is the living spec/TODO tracker.
- Remaining explicit README follow-ups are incremental rebuild optimization and s-expression parser work.

### Metis Review
**Identified Gaps** (addressed in this plan):
- Primary persona was not explicitly chosen → default to **C++ API consumers inside `nndeploy`** this quarter.
- Breaking-change policy was not explicit → default to **no intentional public API breakage unless clearly justified by usability and documented in README**.
- Scope creep risk around parser/performance/parity → explicitly deferred unless they unblock quarter goals.
- Acceptance criteria needed to be concrete → every task below includes test and QA requirements.

---

## Work Objectives

### Core Objective
Make the e-graph subsystem safe and practical to adopt inside `nndeploy` by hardening contracts, improving debuggability, expanding regression coverage, and documenting real C++ workflows without opening a broad parity or performance program.

### Concrete Deliverables
- A documented quarter contract for what the e-graph subsystem guarantees and what it explicitly does not yet promise
- A stable, better-tested C++ workflow covering add/merge/rebuild/rewrite/extract/explain usage
- Improved diagnostics and edge-case handling around runner/reporting/proof usability
- Quarter-close adoption checklist and next-quarter decision package

### Definition of Done
- [ ] `make egraph_test` passes from `/home/dbz/project/nndeploy`
- [ ] `./build/egraph_test` passes with all added roadmap tests
- [ ] `framework/include/nndeploy/egraph/README.md` clearly documents supported workflows, guardrails, and quarter deferrals
- [ ] At least one end-to-end smoke-tested C++ workflow demonstrates rewrite + rebuild + extract + explain behavior
- [ ] Final verification wave approves scope fidelity, code quality, and QA evidence

### Must Have
- Stable quarter scope centered on production usability
- TDD where practical for core logic changes
- README remains the living spec/TODO tracker
- No hidden reliance on manual human-only verification

### Must NOT Have (Guardrails)
- No major incremental rebuild redesign this quarter unless re-scoped by user
- No broad egg-parity chase that does not materially improve usability
- No parser expansion as a must-ship quarter requirement unless it becomes a blocker through documented evidence
- No silent public API breakage without README callout and explicit rationale
- No task completion without concrete tests or executable QA scenarios

---

## Verification Strategy (MANDATORY)

> **ZERO HUMAN INTERVENTION** — ALL verification is agent-executed.

### Test Decision
- **Infrastructure exists**: YES
- **Automated tests**: TDD where practical
- **Framework**: GoogleTest via `egraph_test`
- **If TDD**: Each implementation task that changes behavior should add failing tests first, then minimal implementation, then cleanup/refactor

### QA Policy
Every task must include agent-executed QA scenarios with evidence saved to `.sisyphus/evidence/`.

- **Header/library changes**: Use Bash to build `egraph_test`, run focused gtest filters, and capture output
- **Docs/examples**: Use Bash to build referenced smoke tests or compile/run existing test-backed examples where applicable
- **Workflow validation**: Use Bash to run focused end-to-end gtest cases that exercise rewrite/extract/explain flows

Evidence naming convention:
- `.sisyphus/evidence/task-{N}-{scenario}.txt`

---

## Execution Strategy

### Parallel Execution Waves

> The quarter is organized as usability-first foundations, then developer-experience hardening, then adoption/release closure.

```
Wave 1 (Quarter foundation — start immediately):
├── Task 1: Quarter contract + README scope hardening [writing]
├── Task 2: Error/exception contract audit for core APIs [unspecified-high]
├── Task 3: Test fixture and suite maintainability cleanup [quick]
├── Task 4: Explanation usability hardening [unspecified-high]
└── Task 5: Runner/report diagnostic hardening [unspecified-high]

Wave 2 (After Wave 1 — practical developer usability foundations):
├── Task 6: Structural builder ergonomics for RecExpr usage [quick]
├── Task 7: Structural builder ergonomics for Pattern usage [quick]
└── Task 10: Public API stability audit + compatibility notes [deep]

Wave 3 (After Wave 2 — workflow enablement):
├── Task 8: Interop regression pack for rewrite/extract/explain [unspecified-high]
├── Task 9: Cookbook workflow docs + smoke-backed examples [writing]
└── Task 14: Next-quarter decision package (parser vs incremental rebuild) [deep]

Wave 4 (After Wave 3 — adoption and quarter close):
├── Task 11: End-to-end adoption smoke scenario [unspecified-high]
├── Task 12: Edge-case resilience pack [unspecified-high]
└── Task 13: Quarter-close release checklist + support docs [writing]

Wave FINAL (After ALL tasks — 4 parallel reviews):
├── Task F1: Plan compliance audit (oracle)
├── Task F2: Code quality review (unspecified-high)
├── Task F3: Executed QA Replay (unspecified-high)
└── Task F4: Scope fidelity check (deep)
```

### Dependency Matrix

- **1**: - → 9, 10, 13, 14
- **2**: - → 4, 6, 7, 8, 10, 12
- **3**: - → 8, 11, 12
- **4**: - → 8, 9, 11, 12
- **5**: - → 10, 11, 12, 13
- **6**: 2 → 9, 11
- **7**: 2 → 8, 9, 11
- **8**: 3, 4, 7 → 11, 12, 13
- **9**: 1, 4, 6, 7 → 11, 13
- **10**: 1, 2, 5 → 13, 14
- **11**: 3, 4, 6, 7, 8, 9 → FINAL
- **12**: 2, 3, 4, 5, 8 → FINAL
- **13**: 1, 5, 8, 9, 10 → FINAL
- **14**: 1, 10 → FINAL

### Agent Dispatch Summary

- **Wave 1**: T1 → `writing`, T2/T4/T5 → `unspecified-high`, T3 → `quick`
- **Wave 2**: T6/T7 → `quick`, T10 → `deep`
- **Wave 3**: T8 → `unspecified-high`, T9 → `writing`, T14 → `deep`
- **Wave 4**: T11/T12 → `unspecified-high`, T13 → `writing`
- **FINAL**: F1 → `oracle`, F2/F3 → `unspecified-high`, F4 → `deep`

---

## TODOs

> Implementation + tests + QA evidence stay inside the same task.

---

- [x] 1. Quarter contract + README scope hardening

  **What to do**:
  - Update `framework/include/nndeploy/egraph/README.md` so it explicitly defines: target user persona (C++ API consumers), production-usability goals, breaking-change policy, and quarter deferrals.
  - Add a crisp “supported this quarter / deferred this quarter” section.
  - Make the README the authoritative roadmap anchor for the executor.

  **Must NOT do**:
  - Do not promise parser or incremental rebuild delivery this quarter.
  - Do not redefine semantics that current tests already lock in without matching code/test follow-through.

  **Recommended Agent Profile**:
  - **Category**: `writing`
    - Reason: Main deliverable is precise technical documentation and scope control.
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**:
    - `humanizer`: not necessary; precision matters more than tone.

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 2-5)
  - **Blocks**: 9, 10, 13, 14
  - **Blocked By**: None

  **References**:
  - `framework/include/nndeploy/egraph/README.md` - living spec/TODO tracker that must carry the quarter contract
  - `framework/include/nndeploy/egraph/egraph.h:EGraph<Op, Analysis>` - public lifecycle API the README must describe correctly
  - `framework/include/nndeploy/egraph/explain.h:Explanation<Op>` - explanation-facing APIs that need support-status wording
  - `framework/include/nndeploy/egraph/runner.h:Runner<Op, Analysis, OpHash>` - runner semantics to document without overpromising

  **Acceptance Criteria**:
  - [ ] README contains explicit sections for quarter scope, deferrals, breaking-change policy, and target user persona
  - [ ] README text matches actual supported subsystem behavior
  - [ ] `make egraph_test` still passes unchanged after docs update

  **QA Scenarios**:
  ```
  Scenario: Quarter contract is present and auditable
    Tool: Bash
    Preconditions: README update committed in working tree
    Steps:
      1. Search README for headings/phrases covering "quarter", "deferred", and "breaking-change" policy.
      2. Read the updated sections and confirm they name C++ API consumers as the primary quarter persona.
      3. Save matching excerpts to .sisyphus/evidence/task-1-readme-contract.txt.
    Expected Result: The README contains all required scope/guardrail sections with concrete wording.
    Failure Indicators: Missing deferral section, vague persona, or silent promise of parser/performance work.
    Evidence: .sisyphus/evidence/task-1-readme-contract.txt

  Scenario: Docs-only change preserves test baseline
    Tool: Bash
    Preconditions: README update applied
    Steps:
      1. Run `make egraph_test` from `/home/dbz/project/nndeploy`.
      2. Record command output.
    Expected Result: Build succeeds without code/test regressions.
    Failure Indicators: Build failure or unrelated file churn.
    Evidence: .sisyphus/evidence/task-1-build.txt
  ```

  **Commit**: YES
  - Message: `docs(egraph): define quarter usability contract`
  - Files: `framework/include/nndeploy/egraph/README.md`
  - Pre-commit: `make egraph_test`

- [x] 2. Error/exception contract audit for core APIs

  **What to do**:
  - Audit `egraph.h`, `recexpr.h`, and `subst.h` for invalid-id, out-of-range, and invalid-argument behavior.
  - Add/adjust targeted tests so misuse fails consistently and predictably.
  - Standardize high-value exception messages where inconsistency would hurt adoption/debugging.

  **Must NOT do**:
  - Do not redesign core APIs beyond contract clarity.
  - Do not broaden this into a generic refactor of unrelated templates.

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: Cross-header API hardening with correctness-sensitive tests.
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**:
    - `ai-slop-remover`: scope is semantic hardening, not style cleanup.

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 1, 3, 4, 5)
  - **Blocks**: 4, 6, 7, 8, 10, 12
  - **Blocked By**: None

  **References**:
  - `framework/include/nndeploy/egraph/egraph.h:Id / UnionFind / EGraph` - current invalid-id and merge/add error paths
  - `framework/include/nndeploy/egraph/recexpr.h:RecExpr<Op>` - topological validation and empty-expression behavior
  - `framework/include/nndeploy/egraph/subst.h:Subst` - substitution lookup/insert contract
  - `test/source/nndeploy/egraph/egraph_test.cc:EGraphTest.*` - existing safety and misuse regression patterns

  **Acceptance Criteria**:
  - [ ] Focused tests cover foreign ids, invalid defaults, malformed RecExpr/Pattern usage, and missing substitutions via a dedicated `ErrorContractTest.*` suite or equivalent focused test names
  - [ ] Misuse paths throw the expected exception class consistently
  - [ ] `./build/egraph_test --gtest_filter=EGraphTest.*:PatternTest.*` passes

  **QA Scenarios**:
  ```
  Scenario: Invalid IDs and malformed expressions fail predictably
    Tool: Bash
    Preconditions: New/updated error-contract tests exist
    Steps:
      1. Run `./build/egraph_test --gtest_filter=EGraphTest.InvalidIdsThrow:EGraphTest.RecExprRejectsForwardReferences`.
      2. Run any new focused error-contract tests added for Subst/Pattern misuse (prefer `ErrorContractTest.*`).
      3. Save stdout/stderr to .sisyphus/evidence/task-2-error-contract.txt.
    Expected Result: All targeted tests pass and document consistent exception behavior.
    Failure Indicators: Wrong exception class, brittle message semantics, or uncovered misuse path.
    Evidence: .sisyphus/evidence/task-2-error-contract.txt

  Scenario: Core API hardening does not regress full suite
    Tool: Bash
    Preconditions: Header and test changes applied
    Steps:
      1. Run `make egraph_test && ./build/egraph_test`.
      2. Save output.
    Expected Result: Full suite passes.
    Failure Indicators: Any unrelated failure in existing e-graph behavior.
    Evidence: .sisyphus/evidence/task-2-full-suite.txt
  ```

  **Commit**: YES
  - Message: `test(egraph): harden core error contracts`
  - Files: `framework/include/nndeploy/egraph/egraph.h`, `framework/include/nndeploy/egraph/recexpr.h`, `framework/include/nndeploy/egraph/subst.h`, `test/source/nndeploy/egraph/egraph_test.cc`
  - Pre-commit: `make egraph_test && ./build/egraph_test --gtest_filter=EGraphTest.*:PatternTest.*`

- [x] 3. Test fixture and suite maintainability cleanup

  **What to do**:
  - Reduce repetition in `egraph_test.cc` by extracting shared builders/fixtures/helpers for common graph/pattern setup.
  - If needed, introduce one small local test helper header/source pair under `test/source/nndeploy/egraph/` without changing semantics.
  - Make future usability regressions easier to add without growing brittle copy-paste test code.

  **Must NOT do**:
  - Do not rewrite existing tests just for style.
  - Do not split the suite into many files unless it clearly improves maintainability with minimal churn.

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: Focused test-maintenance refactor in 1-3 files.
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**:
    - `refactor`: unnecessary for a localized test-only cleanup.

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 1, 2, 4, 5)
  - **Blocks**: 8, 11, 12
  - **Blocked By**: None

  **References**:
  - `test/source/nndeploy/egraph/egraph_test.cc` - current single-file suite with repeated setup helpers
  - `framework/include/nndeploy/egraph/README.md` - test coverage list that should still map clearly to actual suite structure

  **Acceptance Criteria**:
  - [ ] Shared setup logic is centralized enough that new roadmap tests can reuse it
  - [ ] Existing test names/semantics remain stable unless there is a strong reason to rename
  - [ ] `./build/egraph_test` passes after the cleanup

  **QA Scenarios**:
  ```
  Scenario: Cleanup preserves test behavior
    Tool: Bash
    Preconditions: Test helper cleanup applied
    Steps:
      1. Run `./build/egraph_test`.
      2. Compare total test count and suite names against expected roadmap baseline.
      3. Save output to .sisyphus/evidence/task-3-suite-baseline.txt.
    Expected Result: Test suite still passes and remains readable to future maintainers.
    Failure Indicators: Semantic drift, deleted coverage, or hard-to-trace helper indirection.
    Evidence: .sisyphus/evidence/task-3-suite-baseline.txt

  Scenario: New helper path is actually exercised
    Tool: Bash
    Preconditions: Shared helper introduced
    Steps:
      1. Run one focused filter from each major suite that now uses shared helpers.
      2. Save passing output.
    Expected Result: Reused helpers support multiple suites without hidden coupling.
    Evidence: .sisyphus/evidence/task-3-helper-usage.txt
  ```

  **Commit**: YES
  - Message: `test(egraph): reduce suite setup duplication`
  - Files: `test/source/nndeploy/egraph/egraph_test.cc` (+ optional small local helper file)
  - Pre-commit: `./build/egraph_test`

- [x] 4. Explanation usability hardening

  **What to do**:
  - Polish the newly added explanation/proof-tracing APIs so they are predictable to consume from C++.
  - Expand regression coverage around direct merges, congruence, transitive proofs, already-equal alternate rewrites, and flat proof conversion.
  - Clarify explanation usage and constraints in README.

  **Must NOT do**:
  - Do not redesign the proof forest architecture this quarter.
  - Do not broaden into novel proof-optimization work.

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: Proof semantics are correctness-sensitive and span code/tests/docs.
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**:
    - `writing`: docs matter, but core risk is semantic correctness.

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 1, 2, 3, 5)
  - **Blocks**: 8, 9, 11, 12
  - **Blocked By**: None

  **References**:
  - `framework/include/nndeploy/egraph/explain.h:Explanation<Op> / FlatTerm<Op>` - user-facing proof APIs
  - `framework/include/nndeploy/egraph/egraph.h:Explain<Op, OpHash> / mergeWithJustification()` - proof recording semantics
  - `test/source/nndeploy/egraph/egraph_test.cc:ExplanationTest.*` - current baseline proof coverage
  - `framework/include/nndeploy/egraph/README.md` - explanation feature contract and deferrals

  **Acceptance Criteria**:
  - [ ] Explanation tests cover direct, congruence, transitive, alternate-rewrite, and error paths clearly
  - [ ] README documents how to enable explanations and when free functions throw
  - [ ] `./build/egraph_test --gtest_filter=ExplanationTest.*` passes

  **QA Scenarios**:
  ```
  Scenario: Explanation workflows remain stable
    Tool: Bash
    Preconditions: Explanation API/test/doc updates applied
    Steps:
      1. Run `./build/egraph_test --gtest_filter=ExplanationTest.*`.
      2. Save output to .sisyphus/evidence/task-4-explanations.txt.
    Expected Result: All explanation-specific tests pass.
    Failure Indicators: Missing proof steps, wrong rule direction, or broken flat conversion.
    Evidence: .sisyphus/evidence/task-4-explanations.txt

  Scenario: README explanation guidance matches executable behavior
    Tool: Bash
    Preconditions: README explanation section updated
    Steps:
      1. Verify README mentions enablement, exceptions, and free-function entry points.
      2. Cross-check with the passing explanation test suite.
      3. Save excerpts and test output references.
    Expected Result: Docs do not overstate unsupported proof features.
    Evidence: .sisyphus/evidence/task-4-doc-sync.txt
  ```

  **Commit**: YES
  - Message: `fix(egraph): harden explanation usability`
  - Files: `framework/include/nndeploy/egraph/explain.h`, `framework/include/nndeploy/egraph/egraph.h`, `framework/include/nndeploy/egraph/README.md`, `test/source/nndeploy/egraph/egraph_test.cc`
  - Pre-commit: `./build/egraph_test --gtest_filter=ExplanationTest.*`

- [x] 5. Runner/report diagnostic hardening

  **What to do**:
  - Improve runner-facing diagnostics so misuse and saturation behavior are easier to debug in production-style workflows.
  - Strengthen coverage around stop reasons, report consistency, and rule-application visibility.
  - Clarify any diagnostics that are currently underdocumented in README.

  **Must NOT do**:
  - Do not redesign the scheduler framework.
  - Do not turn this into a performance/profiling project.

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: Behavior changes affect debugability, reporting, and regression expectations.
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 1-4)
  - **Blocks**: 10, 11, 12, 13
  - **Blocked By**: None

  **References**:
  - `framework/include/nndeploy/egraph/runner.h:Runner / Iteration / Report / StopReason` - reporting and stop-state surface
  - `framework/include/nndeploy/egraph/rewrite.h:Rewrite` - per-rule naming/reporting path
  - `test/source/nndeploy/egraph/egraph_test.cc:RunnerTest.*` - current runner/report regression coverage
  - `framework/include/nndeploy/egraph/README.md` - runner semantics documentation

  **Acceptance Criteria**:
  - [ ] Runner/report tests cover expected stop reasons and consistent summary output
  - [ ] Any new diagnostic wording is documented where users will find it
  - [ ] `./build/egraph_test --gtest_filter=RunnerTest.*` passes

  **QA Scenarios**:
  ```
  Scenario: Runner diagnostics remain stable and legible
    Tool: Bash
    Preconditions: Runner/report changes applied
    Steps:
      1. Run `./build/egraph_test --gtest_filter=RunnerTest.*`.
      2. Save output to .sisyphus/evidence/task-5-runner.txt.
    Expected Result: Runner-focused tests pass and cover report/stop-reason behavior.
    Failure Indicators: Broken report summaries, wrong stop-reason transitions, or rule-count regressions.
    Evidence: .sisyphus/evidence/task-5-runner.txt

  Scenario: Full-suite regression check after diagnostic changes
    Tool: Bash
    Preconditions: Runner/report updates complete
    Steps:
      1. Run `./build/egraph_test`.
      2. Save output.
    Expected Result: Diagnostics work does not destabilize unrelated e-graph features.
    Evidence: .sisyphus/evidence/task-5-full-suite.txt
  ```

  **Commit**: YES
  - Message: `fix(egraph): improve runner diagnostics`
  - Files: `framework/include/nndeploy/egraph/runner.h`, `framework/include/nndeploy/egraph/README.md`, `test/source/nndeploy/egraph/egraph_test.cc`
  - Pre-commit: `./build/egraph_test --gtest_filter=RunnerTest.*`

- [x] 6. Structural builder ergonomics for RecExpr usage

  **What to do**:
  - Add small, explicit C++ conveniences that reduce ceremony when building `RecExpr` trees structurally without introducing a parser.
  - Keep topological-order and explicit-child semantics visible to callers.
  - Add focused tests and README examples for the new ergonomic path.

  **Must NOT do**:
  - Do not introduce string parsing.
  - Do not hide validation rules that callers still need to understand.

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: Small public ergonomics addition with tight test/docs loop.
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with Tasks 7-10)
  - **Blocks**: 9, 11
  - **Blocked By**: 2

  **References**:
  - `framework/include/nndeploy/egraph/recexpr.h:RecExpr<Op>` - primary authoring surface to improve
  - `test/source/nndeploy/egraph/egraph_test.cc:EGraphTest.RecExprStoresTopologicallyOrderedNodes` - current baseline semantics to preserve
  - `framework/include/nndeploy/egraph/README.md` - structural workflow docs that should show the ergonomic path

  **Acceptance Criteria**:
  - [ ] New ergonomic entry points remain explicit and preserve existing validation rules
  - [ ] Focused tests cover both happy-path and invalid construction behavior, preferably under `RecExprErgonomicsTest.*`
  - [ ] `./build/egraph_test --gtest_filter=RecExprErgonomicsTest.*:EGraphTest.RecExpr*:ExtractorTest.*` passes

  **QA Scenarios**:
  ```
  Scenario: Ergonomic RecExpr path preserves semantics
    Tool: Bash
    Preconditions: RecExpr helper API and tests added
    Steps:
      1. Run `./build/egraph_test --gtest_filter=RecExprErgonomicsTest.*:EGraphTest.RecExpr*:ExtractorTest.*`.
      2. Save output to .sisyphus/evidence/task-6-recexpr.txt.
    Expected Result: New helpers are usable without changing topological correctness rules.
    Failure Indicators: Forward references slip through or helper path diverges from base behavior.
    Evidence: .sisyphus/evidence/task-6-recexpr.txt

  Scenario: README example compiles against public API expectations
    Tool: Bash
    Preconditions: README ergonomic example updated
    Steps:
      1. Cross-check example structure against passing tests exercising the same helper path.
      2. Save excerpt + test reference.
    Expected Result: Docs describe an actually supported ergonomic workflow.
    Evidence: .sisyphus/evidence/task-6-doc-example.txt
  ```

  **Commit**: YES
  - Message: `feat(egraph): improve recexpr authoring ergonomics`
  - Files: `framework/include/nndeploy/egraph/recexpr.h`, `framework/include/nndeploy/egraph/README.md`, `test/source/nndeploy/egraph/egraph_test.cc`
  - Pre-commit: `./build/egraph_test --gtest_filter=RecExprErgonomicsTest.*:EGraphTest.RecExpr*:ExtractorTest.*`

- [x] 7. Structural builder ergonomics for Pattern usage

  **What to do**:
  - Add small structural conveniences for pattern construction so callers can author `PatternAst` and `ENodeOrVar` trees with less boilerplate while staying parser-free.
  - Preserve repeated-variable and variable-leaf semantics.
  - Cover the new path with focused matcher/rewrite tests.

  **Must NOT do**:
  - Do not add an s-expression parser.
  - Do not weaken repeated-variable or variable-with-children validation.

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: Focused ergonomics improvement around one API surface.
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with Tasks 6, 8, 9, 10)
  - **Blocks**: 8, 9, 11
  - **Blocked By**: 2

  **References**:
  - `framework/include/nndeploy/egraph/pattern.h:Pattern<Op> / PatternAst<Op> / ENodeOrVar<Op>` - structural pattern surface
  - `test/source/nndeploy/egraph/egraph_test.cc:PatternTest.* / MatcherTest.* / RewriteTest.*` - semantic tests to preserve
  - `framework/include/nndeploy/egraph/README.md` - pattern authoring guidance

  **Acceptance Criteria**:
  - [ ] Ergonomic pattern helpers preserve current matcher/rewrite semantics, preferably under `PatternErgonomicsTest.*`
  - [ ] Repeated-variable and malformed-variable tests still pass
  - [ ] `./build/egraph_test --gtest_filter=PatternErgonomicsTest.*:PatternTest.*:MatcherTest.*:RewriteTest.*` passes

  **QA Scenarios**:
  ```
  Scenario: Pattern ergonomic path preserves matcher behavior
    Tool: Bash
    Preconditions: Pattern helper API and tests added
    Steps:
      1. Run `./build/egraph_test --gtest_filter=PatternErgonomicsTest.*:PatternTest.*:MatcherTest.*:RewriteTest.*`.
      2. Save output to .sisyphus/evidence/task-7-patterns.txt.
    Expected Result: Pattern helpers reduce boilerplate without changing semantics.
    Failure Indicators: Repeated-var matching breaks, malformed vars slip through, or rewrite behavior changes unexpectedly.
    Evidence: .sisyphus/evidence/task-7-patterns.txt

  Scenario: README guidance stays parser-free
    Tool: Bash
    Preconditions: README pattern examples updated
    Steps:
      1. Verify docs do not imply string/s-expression parsing exists.
      2. Save excerpt.
    Expected Result: Ergonomics are documented as structural helpers, not parser support.
    Evidence: .sisyphus/evidence/task-7-parser-free-docs.txt
  ```

  **Commit**: YES
  - Message: `feat(egraph): improve pattern authoring ergonomics`
  - Files: `framework/include/nndeploy/egraph/pattern.h`, `framework/include/nndeploy/egraph/README.md`, `test/source/nndeploy/egraph/egraph_test.cc`
  - Pre-commit: `./build/egraph_test --gtest_filter=PatternErgonomicsTest.*:PatternTest.*:MatcherTest.*:RewriteTest.*`

- [x] 8. Interop regression pack for rewrite/extract/explain

  **What to do**:
  - Add focused end-to-end regression tests covering the interaction surface between rewrite application, rebuild, extraction, and explanation.
  - Fix only the concrete interoperability gaps exposed by those tests.
  - Ensure the interop pack reflects quarter-supported workflows, not hypothetical parity features.

  **Must NOT do**:
  - Do not turn this into a broad new feature wave.
  - Do not optimize proof or extraction algorithms beyond correctness/usability needs.

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: Multi-component regression hardening across several correctness-sensitive paths.
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3 (with Tasks 9, 14)
  - **Blocks**: 11, 12, 13
  - **Blocked By**: 3, 4, 7

  **References**:
  - `framework/include/nndeploy/egraph/rewrite.h:Rewrite` - rewrite entry point
  - `framework/include/nndeploy/egraph/extract.h:Extractor` - extraction semantics
  - `framework/include/nndeploy/egraph/explain.h:explainIdEquivalence / explainEquivalence` - proof output surface
  - `test/source/nndeploy/egraph/egraph_test.cc:RewriteTest.* / ExtractorTest.* / ExplanationTest.*` - existing isolated coverage to unify

  **Acceptance Criteria**:
  - [ ] At least one focused test in `InteropWorkflowTest.*` exercises rewrite → rebuild → extract → explain in sequence
  - [ ] Interop regressions are covered without introducing unsupported workflow claims
  - [ ] `./build/egraph_test --gtest_filter=InteropWorkflowTest.*:RewriteTest.*:ExtractorTest.*:ExplanationTest.*` passes

  **QA Scenarios**:
  ```
  Scenario: Interop workflow passes end to end
    Tool: Bash
    Preconditions: Interop tests added
    Steps:
      1. Run `./build/egraph_test --gtest_filter=InteropWorkflowTest.*:RewriteTest.*:ExtractorTest.*:ExplanationTest.*`.
      2. Save output to .sisyphus/evidence/task-8-interop.txt.
    Expected Result: The subsystem supports the quarter's core pipeline without hand-waving between stages.
    Failure Indicators: Rewrite succeeds but extract/explain breaks, or docs promise unsupported sequencing.
    Evidence: .sisyphus/evidence/task-8-interop.txt

  Scenario: Interop changes do not regress broader suite
    Tool: Bash
    Preconditions: Any small fixes from interop testing applied
    Steps:
      1. Run `./build/egraph_test`.
      2. Save output.
    Expected Result: All existing and new tests pass together.
    Evidence: .sisyphus/evidence/task-8-full-suite.txt
  ```

  **Commit**: YES
  - Message: `test(egraph): add interop regression coverage`
  - Files: `test/source/nndeploy/egraph/egraph_test.cc` (+ only minimal supporting header fixes if tests expose them)
  - Pre-commit: `./build/egraph_test --gtest_filter=InteropWorkflowTest.*:RewriteTest.*:ExtractorTest.*:ExplanationTest.*`

- [x] 9. Cookbook workflow docs + smoke-backed examples

  **What to do**:
  - Expand the README with concise cookbook sections for the quarter-supported workflows: structural construction, rewrite application, extraction, explanation, and runner-based saturation.
  - Tie each documented workflow to an executable smoke test or focused gtest scenario so the docs do not drift.
  - Keep examples centered on C++ consumers, not parser-based or Python-first usage.

  **Must NOT do**:
  - Do not introduce tutorial sprawl.
  - Do not document workflows that are not backed by tests.

  **Recommended Agent Profile**:
  - **Category**: `writing`
    - Reason: Primary outcome is usable technical guidance with evidence-backed examples.
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3 (with Tasks 8, 14)
  - **Blocks**: 11, 13
  - **Blocked By**: 1, 4, 6, 7

  **References**:
  - `framework/include/nndeploy/egraph/README.md` - cookbook destination and living spec
  - `framework/include/nndeploy/egraph/egraph.h / rewrite.h / extract.h / explain.h / runner.h` - public APIs the examples must match
  - `test/source/nndeploy/egraph/egraph_test.cc` - smoke-test source of truth for the documented workflows

  **Acceptance Criteria**:
  - [ ] README includes cookbook subsections for the quarter-supported workflows
  - [ ] Each cookbook workflow maps to at least one passing smoke test or focused gtest case (prefer `CookbookWorkflowTest.*` or `SmokeTest.*` names)
  - [ ] `make egraph_test && ./build/egraph_test` passes after doc/example updates

  **QA Scenarios**:
  ```
  Scenario: README workflows are backed by executable tests
    Tool: Bash
    Preconditions: Cookbook sections and smoke tests added
    Steps:
      1. Read the cookbook sections and list the named workflows.
      2. Run the matching focused gtest filters for each workflow.
      3. Save the mapping and outputs to .sisyphus/evidence/task-9-cookbook.txt.
    Expected Result: Every documented workflow has a passing executable counterpart.
    Failure Indicators: README examples that cannot be traced to a test, or tests covering undocumented behavior only.
    Evidence: .sisyphus/evidence/task-9-cookbook.txt

  Scenario: Cookbook updates preserve full-suite stability
    Tool: Bash
    Preconditions: Docs/tests updated
    Steps:
      1. Run `make egraph_test && ./build/egraph_test`.
      2. Save output.
    Expected Result: Full suite passes.
    Evidence: .sisyphus/evidence/task-9-full-suite.txt
  ```

  **Commit**: YES
  - Message: `docs(egraph): add smoke-tested cookbook workflows`
  - Files: `framework/include/nndeploy/egraph/README.md`, `test/source/nndeploy/egraph/egraph_test.cc`
  - Pre-commit: `make egraph_test && ./build/egraph_test`

- [x] 10. Public API stability audit + compatibility notes

  **What to do**:
  - Audit the quarter-exposed public surface for consistency, naming clarity, and accidental instability.
  - Add compatibility notes in README for anything intentionally provisional.
  - Add focused adoption-smoke coverage that exercises the intended public entry points directly.

  **Must NOT do**:
  - Do not perform broad rename churn without usability justification.
  - Do not mark unstable APIs as stable if the test/docs evidence is weak.

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: Requires deliberate judgment about public-surface promises and scope boundaries.
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with Tasks 6, 7)
  - **Blocks**: 13, 14
  - **Blocked By**: 1, 2, 5

  **References**:
  - `framework/include/nndeploy/egraph/egraph.h` - primary public surface
  - `framework/include/nndeploy/egraph/pattern.h` - public authoring/matching surface
  - `framework/include/nndeploy/egraph/explain.h` - explanation API promises
  - `framework/include/nndeploy/egraph/runner.h` - orchestration/reporting surface
  - `framework/include/nndeploy/egraph/README.md` - compatibility wording and provisional API notes

  **Acceptance Criteria**:
  - [ ] README clearly distinguishes stable quarter APIs from provisional conveniences
  - [ ] Adoption-smoke tests compile and execute through the intended public entry points, preferably under `ApiStabilitySmokeTest.*`
  - [ ] `./build/egraph_test --gtest_filter=ApiStabilitySmokeTest.*:EGraphTest.*:RewriteTest.*:ExplanationTest.*:RunnerTest.*` passes

  **QA Scenarios**:
  ```
  Scenario: Public entry points used in intended combinations still work
    Tool: Bash
    Preconditions: API audit and smoke tests added
    Steps:
      1. Run `./build/egraph_test --gtest_filter=ApiStabilitySmokeTest.*:EGraphTest.*:RewriteTest.*:ExplanationTest.*:RunnerTest.*`.
      2. Save output to .sisyphus/evidence/task-10-api-smoke.txt.
    Expected Result: Quarter-supported entry points remain coherent and documented.
    Failure Indicators: Public APIs require hidden knowledge, or provisional APIs are undocumented.
    Evidence: .sisyphus/evidence/task-10-api-smoke.txt

  Scenario: Compatibility notes exist for provisional surfaces
    Tool: Bash
    Preconditions: README compatibility notes updated
    Steps:
      1. Search README for explicit compatibility/provisional wording.
      2. Save excerpts.
    Expected Result: Users can tell what is safe to rely on this quarter.
    Evidence: .sisyphus/evidence/task-10-compatibility.txt
  ```

  **Commit**: YES
  - Message: `docs(egraph): clarify public api stability`
  - Files: `framework/include/nndeploy/egraph/README.md`, `test/source/nndeploy/egraph/egraph_test.cc` (+ only minimal public-surface fixes justified by audit)
  - Pre-commit: `./build/egraph_test --gtest_filter=ApiStabilitySmokeTest.*:EGraphTest.*:RewriteTest.*:ExplanationTest.*:RunnerTest.*`

- [x] 11. End-to-end adoption smoke scenario

  **What to do**:
  - Add one clean, high-value end-to-end test/workflow that models expected quarter adoption: build expression, run rewrite(s), rebuild, extract best term, and explain equivalence.
  - Ensure the scenario exercises the supported ergonomic path introduced in Wave 2 where applicable.
  - Use it as the canonical smoke check for quarter-close usability.

  **Must NOT do**:
  - Do not depend on deferred parser or performance work.
  - Do not hide steps behind undocumented helper magic.

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: Cross-cutting integration check spanning multiple quarter deliverables.
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 4 (with Tasks 12, 13)
  - **Blocks**: FINAL
  - **Blocked By**: 3, 4, 6, 7, 8, 9

  **References**:
  - `framework/include/nndeploy/egraph/egraph.h` - expression/class lifecycle
  - `framework/include/nndeploy/egraph/rewrite.h` - rewrite application path
  - `framework/include/nndeploy/egraph/extract.h` - extraction path
  - `framework/include/nndeploy/egraph/explain.h` - proof path
  - `framework/include/nndeploy/egraph/runner.h` - optional runner-backed orchestration path
  - `test/source/nndeploy/egraph/egraph_test.cc` - destination for the canonical smoke scenario

  **Acceptance Criteria**:
  - [ ] One focused test named `SmokeTest.EndToEndAdoptionFlow` (or equivalent explicit smoke name) covers the quarter’s canonical adoption flow end to end
  - [ ] README points to that workflow as the canonical smoke scenario
  - [ ] `./build/egraph_test --gtest_filter=SmokeTest.EndToEndAdoptionFlow` passes

  **QA Scenarios**:
  ```
  Scenario: Canonical adoption flow passes from a clean state
    Tool: Bash
    Preconditions: End-to-end smoke test added
    Steps:
      1. Build `egraph_test`.
      2. Run `./build/egraph_test --gtest_filter=SmokeTest.EndToEndAdoptionFlow`.
      3. Save output to .sisyphus/evidence/task-11-smoke.txt.
    Expected Result: One executable scenario demonstrates the quarter’s supported adoption path.
    Failure Indicators: Flow breaks between stages, hidden prerequisites, or unsupported helper assumptions.
    Evidence: .sisyphus/evidence/task-11-smoke.txt

  Scenario: Smoke flow remains compatible with full suite
    Tool: Bash
    Preconditions: Smoke scenario integrated
    Steps:
      1. Run `./build/egraph_test`.
      2. Save output.
    Expected Result: Canonical smoke scenario coexists with all prior tests.
    Evidence: .sisyphus/evidence/task-11-full-suite.txt
  ```

  **Commit**: YES
  - Message: `test(egraph): add adoption smoke scenario`
  - Files: `test/source/nndeploy/egraph/egraph_test.cc`, `framework/include/nndeploy/egraph/README.md`
  - Pre-commit: `./build/egraph_test --gtest_filter=*Smoke*`

- [x] 12. Edge-case resilience pack

  **What to do**:
  - Add targeted regression coverage for edge cases most likely to hurt production usability: already-equal merges, empty/no-match paths, cycle-only extraction failures, explanation misuse, and saturation corner cases.
  - Fix only concrete edge-case bugs exposed by the new tests.

  **Must NOT do**:
  - Do not introduce speculative complexity for edge cases not exercised by tests.
  - Do not spin this into a rewrite-engine redesign.

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: Edge-case correctness across several subsystem boundaries.
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 4 (with Tasks 11, 13)
  - **Blocks**: FINAL
  - **Blocked By**: 2, 3, 4, 5, 8

  **References**:
  - `framework/include/nndeploy/egraph/egraph.h` - merge/add/rebuild error paths
  - `framework/include/nndeploy/egraph/extract.h` - cycle-only extraction failure contract
  - `framework/include/nndeploy/egraph/explain.h` - misuse/error behavior for explanations
  - `framework/include/nndeploy/egraph/runner.h` - saturation/limit corner cases
  - `test/source/nndeploy/egraph/egraph_test.cc` - home for the resilience pack

  **Acceptance Criteria**:
  - [ ] Edge-case tests cover already-equal, no-match, cycle-only, misuse, and limit/corner behaviors under an explicit `EdgeCaseResilienceTest.*` suite or equivalent
  - [ ] Only evidenced bugs are fixed
  - [ ] Focused filters plus full suite pass

  **QA Scenarios**:
  ```
  Scenario: Edge-case regression pack passes
    Tool: Bash
    Preconditions: Edge-case tests added
    Steps:
      1. Run focused filters covering explanation, extractor, and runner corner cases (prefer `EdgeCaseResilienceTest.*:ExplanationTest.*:ExtractorTest.*:RunnerTest.*`).
      2. Save output to .sisyphus/evidence/task-12-edge-cases.txt.
    Expected Result: High-risk usability edge cases are executable and green.
    Failure Indicators: Uncaught misuse, broken cycle contract, or unstable already-equal behavior.
    Evidence: .sisyphus/evidence/task-12-edge-cases.txt

  Scenario: Full suite remains green after resilience fixes
    Tool: Bash
    Preconditions: Any minimal bug fixes applied
    Steps:
      1. Run `./build/egraph_test`.
      2. Save output.
    Expected Result: Full suite passes.
    Evidence: .sisyphus/evidence/task-12-full-suite.txt
  ```

  **Commit**: YES
  - Message: `test(egraph): add edge-case resilience coverage`
  - Files: `test/source/nndeploy/egraph/egraph_test.cc` (+ only minimal bug-fix headers justified by tests)
  - Pre-commit: `./build/egraph_test`

- [x] 13. Quarter-close release checklist + support docs

  **What to do**:
  - Add a quarter-close section to README covering adoption checklist, support expectations, evidence required before declaring the subsystem ready for broader internal use, and explicit deferred items.
  - Tie the checklist to the canonical smoke scenario and resilience pack from Tasks 11-12.

  **Must NOT do**:
  - Do not present the subsystem as universally production-ready beyond the tested quarter scope.
  - Do not duplicate checklist content in multiple docs without reason.

  **Recommended Agent Profile**:
  - **Category**: `writing`
    - Reason: Release-readiness framing and support guardrails are documentation-led.
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 4 (with Tasks 11, 12)
  - **Blocks**: FINAL
  - **Blocked By**: 1, 5, 8, 9, 10

  **References**:
  - `framework/include/nndeploy/egraph/README.md` - checklist destination
  - `test/source/nndeploy/egraph/egraph_test.cc` - smoke/resilience tests the checklist should point to
  - `framework/include/nndeploy/egraph/runner.h / explain.h / extract.h` - user-visible workflow surfaces mentioned in readiness criteria

  **Acceptance Criteria**:
  - [ ] README includes a clear quarter-close checklist with executable references
  - [ ] Deferred items remain explicit
  - [ ] `make egraph_test` still passes

  **QA Scenarios**:
  ```
  Scenario: Release checklist is concrete and executable
    Tool: Bash
    Preconditions: README checklist added
    Steps:
      1. Read checklist entries and confirm each maps to a command or test target.
      2. Save excerpts to .sisyphus/evidence/task-13-checklist.txt.
    Expected Result: Checklist is actionable, not aspirational.
    Failure Indicators: Vague signoff language or missing test/command references.
    Evidence: .sisyphus/evidence/task-13-checklist.txt

  Scenario: Docs closure preserves build baseline
    Tool: Bash
    Preconditions: README updated
    Steps:
      1. Run `make egraph_test`.
      2. Save output.
    Expected Result: Docs updates do not disturb build baseline.
    Evidence: .sisyphus/evidence/task-13-build.txt
  ```

  **Commit**: YES
  - Message: `docs(egraph): add quarter close checklist`
  - Files: `framework/include/nndeploy/egraph/README.md`
  - Pre-commit: `make egraph_test`

- [x] 14. Next-quarter decision package (parser vs incremental rebuild)

  **What to do**:
  - Add an evidence-based decision section to README that explains when parser work should become a must-ship item next quarter versus when incremental rebuild should be prioritized first.
  - Base the decision package on quarter learnings from ergonomic friction, smoke scenario pain points, and resilience findings.
  - Keep this as a prioritization artifact, not implementation work.

  **Must NOT do**:
  - Do not start implementing parser or incremental rebuild here.
  - Do not make the decision package speculative; it must cite quarter evidence.

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: This is a prioritization/architecture trade-off artifact.
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3 (with Tasks 8, 9)
  - **Blocks**: FINAL
  - **Blocked By**: 1, 10

  **References**:
  - `framework/include/nndeploy/egraph/README.md` - decision-package destination and current TODO source
  - `framework/include/nndeploy/egraph/recexpr.h / pattern.h` - structural ergonomics pain points relevant to parser prioritization
  - `framework/include/nndeploy/egraph/egraph.h` - rebuild complexity and correctness surface relevant to incremental rebuild prioritization
  - `test/source/nndeploy/egraph/egraph_test.cc` - evidence source from quarter smoke and resilience coverage

  **Acceptance Criteria**:
  - [ ] README contains explicit decision criteria for parser-first vs incremental-rebuild-first next quarter
  - [ ] Decision criteria cite concrete quarter evidence rather than intuition alone
  - [ ] No implementation of deferred work is introduced

  **QA Scenarios**:
  ```
  Scenario: Next-quarter decision package is evidence-based
    Tool: Bash
    Preconditions: README decision section updated
    Steps:
      1. Read the decision section and identify cited quarter evidence.
      2. Cross-check that the cited evidence exists in tests/docs from Tasks 9-13.
      3. Save notes to .sisyphus/evidence/task-14-decision-package.txt.
    Expected Result: Next-quarter prioritization is grounded in this quarter's validated work.
    Failure Indicators: Pure speculation or hidden implementation creep.
    Evidence: .sisyphus/evidence/task-14-decision-package.txt

  Scenario: Deferred work remains deferred
    Tool: Bash
    Preconditions: Decision package added
    Steps:
      1. Review diff summary for parser/rebuild implementation churn.
      2. Save summary.
    Expected Result: No implementation of deferred items appears in this task.
    Evidence: .sisyphus/evidence/task-14-deferred.txt
  ```

  **Commit**: YES
  - Message: `docs(egraph): add next quarter decision criteria`
  - Files: `framework/include/nndeploy/egraph/README.md`
  - Pre-commit: `make egraph_test`

## Final Verification Wave (MANDATORY — after ALL implementation tasks)

> 4 review agents run in PARALLEL. ALL must approve before completion.

- [ ] F1. **Plan Compliance Audit** — `oracle`
  Verify every must-have deliverable exists, every explicit deferral stayed deferred, README scope matches implementation, and evidence files exist.

  **QA Scenarios**:
  ```
  Scenario: Must-haves and deferrals are both enforced
    Tool: Read + Bash
    Preconditions: All implementation tasks complete and evidence files exist
    Steps:
      1. Read this roadmap and enumerate all Must Have and Must NOT Have items.
      2. Inspect changed README/tests/headers and confirm each must-have is present.
      3. Check that parser and incremental rebuild implementation work did not appear unless explicitly re-scoped.
      4. Save findings to .sisyphus/evidence/final-f1-plan-compliance.txt.
    Expected Result: Every planned deliverable exists and every explicit deferral remains respected.
    Failure Indicators: Missing deliverable, undocumented API break, or deferred work implemented silently.
    Evidence: .sisyphus/evidence/final-f1-plan-compliance.txt
  ```

- [ ] F2. **Code Quality Review** — `unspecified-high`
  Run build/tests, inspect changed headers/tests/docs for exception-contract regressions, generic-name slop, dead comments, and scope leakage.

  **QA Scenarios**:
  ```
  Scenario: Build and suite are clean at review time
    Tool: Bash
    Preconditions: All implementation tasks complete
    Steps:
      1. Run `make egraph_test && ./build/egraph_test`.
      2. Review changed files for dead comments, generic placeholder names, and exception-contract regressions.
      3. Save results to .sisyphus/evidence/final-f2-code-quality.txt.
    Expected Result: Build succeeds, tests pass, and changed files are clean and scoped.
    Failure Indicators: Build/test failure, sloppy naming, or low-signal churn.
    Evidence: .sisyphus/evidence/final-f2-code-quality.txt
  ```

- [ ] F3. **Executed QA Replay** — `unspecified-high`
  Execute every task QA scenario, plus one clean end-to-end smoke workflow that exercises add/merge/rebuild/rewrite/extract/explain together.

  **QA Scenarios**:
  ```
  Scenario: Task-level evidence is reproducible end to end
    Tool: Bash
    Preconditions: All task evidence files exist
    Steps:
      1. Re-run the canonical smoke scenario (`SmokeTest.EndToEndAdoptionFlow`).
      2. Re-run focused filters for explanations, runner behavior, and edge-case resilience.
      3. Save outputs to .sisyphus/evidence/final-f3-qa-replay.txt.
    Expected Result: Quarter-critical workflows are reproducible from a clean test run.
    Failure Indicators: Any smoke/regression scenario fails or evidence cannot be reproduced.
    Evidence: .sisyphus/evidence/final-f3-qa-replay.txt
  ```

- [ ] F4. **Scope Fidelity Check** — `deep`
  Compare actual changes against this roadmap; reject performance/program-parity work that slipped in without quarter-scope justification.

  **QA Scenarios**:
  ```
  Scenario: Actual diff matches roadmap scope
    Tool: Read + Bash
    Preconditions: All implementation work complete
    Steps:
      1. Review the final diff and group changes by task.
      2. Compare actual work against this roadmap's task list and explicit deferrals.
      3. Save conclusions to .sisyphus/evidence/final-f4-scope-fidelity.txt.
    Expected Result: No major scope creep, no silent deferred-work implementation, and no missing core roadmap deliverables.
    Failure Indicators: Parser/performance creep, missing smoke/doc work, or cross-task contamination.
    Evidence: .sisyphus/evidence/final-f4-scope-fidelity.txt
  ```

---

## Commit Strategy

- **Wave 1 docs/contracts**: `docs(egraph): define quarter usability contract`
- **Wave 1 hardening**: `test(egraph): expand regression coverage for errors and explanations`
- **Wave 2 ergonomics**: `feat(egraph): improve structural authoring ergonomics`
- **Wave 2 workflows**: `docs(egraph): add smoke-tested workflow guidance`
- **Wave 3 closure**: `docs(egraph): add release checklist and next-quarter decision package`

---

## Success Criteria

### Verification Commands
```bash
make egraph_test                      # Expected: build succeeds
./build/egraph_test                  # Expected: all tests pass
./build/egraph_test --gtest_filter=ExplanationTest.*  # Expected: explanation-focused tests pass
./build/egraph_test --gtest_filter=RunnerTest.*       # Expected: runner-focused tests pass
./build/egraph_test --gtest_filter=MultiPatternTest.* # Expected: multi-pattern workflows pass
```

### Final Checklist
- [ ] Quarter contract and deferrals are explicit in README
- [ ] Public C++ workflows are documented and smoke-tested
- [ ] New usability work is covered by targeted regression tests
- [ ] Diagnostics are strong enough to debug misuse and saturation behavior
- [ ] End-to-end adoption scenario passes cleanly
- [ ] Parser and incremental rebuild are either still deferred or explicitly re-scoped by user
