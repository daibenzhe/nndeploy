# egraph module specification

## Quarter Contract (Q2 2026)

This section defines the production-usability goals and commitments for the `nndeploy::egraph` subsystem this quarter.

### Primary Persona
The target audience is **C++ API consumers** building rewrite systems or graph optimizers inside the `nndeploy` framework. The focus is on providing a stable, well-defined, and ergonomic C++ interface for programmatic e-graph manipulation.

### Supported This Quarter
- **Contract & Error Hardening**: Explicit error behavior for invalid Ids and state transitions.
- **Ergonomics Improvements**: Simplified `RecExpr` and `Pattern` construction workflows.
- **Regression Coverage**: Comprehensive test suite ensuring core e-graph and saturation invariants.
- **Cookbook Workflows**: Documented best practices for common optimization patterns.
- **Adoption Smoke Tests**: End-to-end verification of e-graph integration in `nndeploy` components.
- **Release Checklist**: Formalized verification steps for public API stability.

The canonical quarter smoke check is `SmokeTest.EndToEndAdoptionFlow` in `test/source/nndeploy/egraph/egraph_test.cc`. Run it with: `./build/egraph_test --gtest_filter=SmokeTest.EndToEndAdoptionFlow`

### Deferred This Quarter
- **S-expression Parser**: Broad support for string-based pattern/expression parsing is deferred. Patterns must be constructed via `PatternAst`, `PatternBuilder`, or `RecExpr`.
- **Incremental Rebuild Redesign**: While `rebuild()` is correct, the transition to a high-performance incremental model is deferred.
- **Performance Parity Chase**: Matching the raw throughput of the Rust `egg` library is not a goal this quarter. Correctness and usability take precedence.

### Breaking-Change Policy
We commit to **no silent public API breakage**. Any unavoidable breaking changes to the public API must be:
1. Documented in this README with a clear rationale.
2. Accompanied by a migration path for internal consumers.
3. Explicitly called out in the release notes.

## Public API stability status

This quarter, the recommended public surface for `nndeploy::egraph` is split into
stable entry points and intentionally provisional hooks.

### Supported as stable this quarter

The following APIs are the supported adoption path for internal C++ consumers and
are covered by focused smoke tests:

- `EGraph::add()`, `EGraph::addExpr(const RecExpr<Op>&)`, `EGraph::merge()`, and `EGraph::rebuild()`
- `RecExpr<Op>` construction via `add()`, `root()`, and `rootId()`
- `Pattern<Op>`, `PatternSearcher`, and pattern search via `search()` / `searchEclass()`
- `Rewrite<Op,...>` with `Runner<Op,...>` for rewrite application and saturation
- `Extractor<Op,...>` via `findBest()`, `findBestCost()`, and `findBestNode()`
- explanation entry points intended for callers: `withExplanationsEnabled()`, `explainIdEquivalence()`, and `explainEquivalence()`
- `Runner` configuration (`withIterLimit`, `withNodeLimit`, `withTimeLimit`, `withScheduler`, `withHook`) and run summaries (`iterations`, `stop_reason`, `report()`)

### Provisional / likely-to-change APIs

These APIs are public today, but should be treated as lower-level integration
hooks rather than long-term stable contracts:

- Low-level explanation internals: `Explain`, `ExplainNode`, `Connection`, `egraph.explain()`, and `nodeStorage()`. Prefer the free functions `explainIdEquivalence()` and `explainEquivalence()` unless you are extending proof reconstruction itself.
- Legacy expression insertion helper `addExpr(const Node&, const std::vector<Node>&)`. Prefer `addExpr(const RecExpr<Op>&)` for new code.
- Global rebuild implementation details and performance characteristics. `rebuild()` correctness is part of the contract; its current whole-graph strategy and `dirty_roots_` trigger model are not.

### Naming and compatibility notes

- `merge()` is the plain equivalence operation; `unionTrusted()` is the rule-annotated convenience for explanation-aware merges. Both are retained for compatibility this quarter.
- `contains(Id)` currently follows the module's invalid-id contract and may throw `std::out_of_range` for invalid or foreign ids instead of returning `false`. Treat that throwing behavior as part of the current contract.
- `searchEclass` / `searchEclassWithSubsts` keep the existing `EClass`-spelled naming for compatibility with the rest of the module.

## Overview

`nndeploy::egraph` is a C++17 template-based implementation of the core e-graph data structures inspired by the Rust `egg` library.

The current scope is intentionally limited to the structural core:

- `Id`
- `UnionFind`
- `ENode<Op>`
- `EClass<Op, Analysis>`
- `EGraph<Op, Analysis>`
- `RecExpr<Op>`
- `Var`
- `Subst`
- `ENodeOrVar<Op>`
- `PatternAst<Op>`
- `PatternBuilder<Op>`
- `Pattern<Op>`
- `SearchMatches<Op>`
- `Searcher<Op, Analysis, OpHash>`
- `Applier<Op, Analysis, OpHash>`
- `PatternSearcher<Op, Analysis, OpHash>`
- `PatternApplier<Op, Analysis, OpHash>`
- `Rewrite<Op, Analysis, OpHash>`
- `AstSize`
- `AstDepth`
- `Extractor<Op, Analysis, CostFn, OpHash>`
- `StopReason`
- `Iteration`
- `Report`
- `RewriteScheduler<Op, Analysis, OpHash>`
- `SimpleScheduler<Op, Analysis, OpHash>`
- `BackoffScheduler<Op, Analysis, OpHash>`
- `Runner<Op, Analysis, OpHash>`
- `MultiPattern<Op>`
- `MultiPatternSearcher<Op, Analysis, OpHash>`
- `JustificationKind`
- `Justification`
- `Connection`
- `ExplainNode`
- `Explain<Op, OpHash>`
- `TreeTerm<Op>`
- `TreeExplanation<Op>`
- `FlatTerm<Op>`
- `FlatExplanation<Op>`
- `Explanation<Op>`
- `explainIdEquivalence(egraph, left, right)` (free function)
- `explainEquivalence(egraph, left_expr, right_expr)` (free function)
- hashconsing through a memo table
- congruence restoration through `rebuild()`
- pluggable analysis merge through the `Analysis` template parameter
- pattern matching with variable binding and equality constraints
- generic Searcher/Applier interfaces for custom rewrite logic
- pattern-based and custom rewrite rules
- greedy bottom-up extraction with pluggable cost functions
- iterative equality saturation via Runner with pluggable schedulers
- multi-pattern matching with shared variables across multiple patterns
- explanation / proof tracing with justification recording and proof reconstruction

This module provides a complete equality saturation pipeline: e-graph construction, pattern matching, multi-pattern matching, generic Searcher/Applier interfaces for custom rewrite logic, rewrite rules, extraction, an iterative runner with pluggable scheduling, and explanation/proof tracing for merge justifications.

## Cookbook Workflows

This section provides concise, executable snippets for common e-graph tasks. Each workflow is backed by an automated smoke test.

### 1. Structural expression construction

Use `RecExprBuilder` to construct expressions without manual index bookkeeping.

```cpp
RecExprBuilder<MyOp> builder;
Id a = builder.addLeaf(MyOp{"a"});
Id zero = builder.addLeaf(MyOp{"0"});
builder.addNode(MyOp{"+"}, {a, zero});

RecExpr<MyOp> expr = std::move(builder).build();
```
*Backed by: `CookbookWorkflowTest.StructuralExpressionConstruction`*

### 2. Structural pattern authoring

Use `PatternBuilder` to define patterns with variables for matching.

```cpp
PatternBuilder<MyOp> builder;
auto x = builder.appendVar("?x");
auto zero = builder.appendNode(MyOp{"0"});
builder.appendNode(MyOp{"+"}, {x, zero});

Pattern<MyOp> pat = std::move(builder).buildPattern();
```
*Backed by: `CookbookWorkflowTest.StructuralPatternAuthoring`*

### 3. Rewrite application + rebuild

Manually apply a rewrite rule and restore congruence.

```cpp
EGraph<MyOp> graph;
Id a = graph.add(make_node("a"));
Id zero = graph.add(make_node("0"));
Id plus = graph.add(make_node("+", {a, zero}));

Rewrite<MyOp> rule = make_plus_zero_rule();
rule.run(graph);
graph.rebuild(); // Must rebuild to restore congruence
```
*Backed by: `CookbookWorkflowTest.RewriteApplicationAndRebuild`*

### 4. Extraction

Find the cheapest equivalent term for a given e-class.

```cpp
Extractor<MyOp> extractor(graph);
// Find cheapest RecExpr in the class containing 'plus'
auto [cost, best] = extractor.findBest(plus);
```
*Backed by: `CookbookWorkflowTest.ExtractionBestTerm`*

### 5. Explanation

Explain why two terms are equivalent by producing a proof.

```cpp
graph.withExplanationsEnabled(); // Must be called on empty graph
// ... add nodes and merge ...
auto expl = explainIdEquivalence(graph, id1, id2);
const auto& flat = expl.flatExplanation(); // Get step-by-step proof
```
*Backed by: `CookbookWorkflowTest.ExplanationEquivalence`*

### 6. Runner-based saturation

Use the `Runner` to automatically apply rules until the graph saturates.

```cpp
Runner<MyOp> runner;
runner.addExpr(expr);
runner.run(rules);

if (runner.stop_reason.kind == StopReasonKind::Saturated) {
    // Optimization complete
}
```
*Backed by: `CookbookWorkflowTest.RunnerSaturation`*

## Design goals

The module is designed to provide:

1. A small, header-only template core that is easy to embed in optimizer code.
2. A generic node representation where the operator type is controlled by the caller.
3. Deterministic structural deduplication for equivalent enodes.
4. Correct congruence closure after merges via `rebuild()`.
5. A minimal analysis interface for per-eclass data accumulation.
6. Pattern matching and rewrite application following `egg` semantics.
7. Generic Searcher/Applier interfaces for custom rewrite logic beyond patterns.
8. An iterative equality saturation runner with pluggable schedulers.
9. Multi-pattern matching where multiple patterns share variables and must all match simultaneously.
10. Explanation / proof tracing: opt-in recording of merge justifications and proof reconstruction.

## Main types

### `Id`

`Id` is the opaque handle for e-nodes and e-classes.

- Backed by `std::size_t`
- Has an invalid sentinel value
- Supports equality and ordering

`Id` values are canonicalized through `UnionFind`.

### `UnionFind`

`UnionFind` provides:

- `makeSet()`
- `find(Id)` with path compression
- `unite(Id, Id)` with union by rank

Its job is to maintain canonical representatives for merged e-classes.

### `ENode<Op>`

`ENode<Op>` is the canonical node shape stored inside the e-graph.

- `op`: operator payload supplied by the caller
- `children`: vector of child `Id`s

Two enodes are equal if both `op` and `children` are equal.

The caller must provide:

- `bool Op::operator==(const Op&) const`
- `std::hash<Op>` or a compatible hash functor

### `EClass<Op, Analysis>`

`EClass` stores one congruence class.

- `id`: canonical class id
- `nodes`: equivalent enodes in the class
- `parents`: parent class ids that reference this class
- `data`: analysis payload

### `NoAnalysis`

`NoAnalysis` is the default analysis policy.

It provides:

- an empty `Data` type
- `make(node)`
- `merge(dst, src)`

It is suitable when no per-class analysis data is needed.

### `EGraph<Op, Analysis>`

`EGraph` owns the main data structures:

- union-find state
- class table
- memo table for hashconsing
- dirty root set used to trigger rebuild

### `RecExpr<Op>`

`RecExpr` stores a topologically ordered expression as a vector of enodes.

- each child id must refer to an earlier node in the same expression
- `root()` and `rootId()` refer to the last node
- invalid forward references are rejected with `std::out_of_range`

### `RecExprBuilder<Op>`

`RecExprBuilder` is a small convenience wrapper for constructing `RecExpr`
trees one node at a time.

- `addLeaf(op)`: append a leaf node
- `addNode(op, {child_ids})`: append a node whose children must already exist
- `expr()`: inspect the growing expression
- `build()`: move out the finished `RecExpr`

The helper keeps the same topological-order rule as `RecExpr`; it only removes
manual index bookkeeping.

```cpp
RecExprBuilder<MyOp> builder;
Id a = builder.addLeaf(MyOp{"a"});
Id zero = builder.addLeaf(MyOp{"0"});
Id plus = builder.addNode(MyOp{"+"}, {a, zero});

const RecExpr<MyOp> &view = builder.expr();
RecExpr<MyOp> expr = std::move(builder).build();
```

### `Var`

`Var` represents a pattern variable.

Supported forms:

- symbolic: `?x`
- numeric: `?#12`

### `Subst`

`Subst` is a small substitution map from `Var` to `Id`.

- `insert(var, id)` updates or appends
- `get(var)` returns a nullable pointer
- `at(var)` throws if the variable is absent

### `ENodeOrVar<Op>`

`ENodeOrVar<Op>` is a tagged union that holds either an `ENode<Op>` or a `Var`.

It mirrors `egg`'s `ENodeOrVar<L>` and is the building block of pattern ASTs.

- `fromENode(node)` creates an operator variant
- `fromVar(var)` creates a variable variant
- `isENode()` / `isVar()` test the tag
- `enode()` / `var()` access the payload (throw on wrong tag)
- `children()` returns the children vector for ENode, empty for Var

### `PatternAst<Op>`

`PatternAst<Op>` is a type alias for `RecExpr<ENodeOrVar<Op>>`.

It stores a topologically ordered pattern expression where each node is either a concrete operator or a pattern variable. The same validation rules as `RecExpr` apply: children must be backward references.

### `PatternBuilder<Op>`

`PatternBuilder<Op>` is a small parser-free helper for building `PatternAst<Op>` values with less boilerplate.

API:

- `appendVar(name)` / `append(makePatternVar<Op>(name))`: append a variable leaf
- `appendNode(op, children)` / `append(makePatternNode<Op>(op, children))`: append an operator node
- `buildAst()`: return the accumulated `PatternAst`
- `buildPattern()`: return a validated `Pattern`

Cookbook example:

```cpp
PatternBuilder<MyOp> builder;
auto x = builder.appendVar("?x");
auto zero = builder.appendNode(MyOp{"0"});
builder.appendNode(MyOp{"+"}, {x, zero});

Pattern<MyOp> pat = std::move(builder).buildPattern();
```

Notes:

- Reusing the same variable name multiple times preserves repeated-variable semantics.
- These helpers do not parse strings or s-expressions; they only reduce AST construction boilerplate.

### `SearchMatches<Op>`

`SearchMatches<Op>` is the result of matching a pattern against one e-class.

- `eclass`: the canonical `Id` of the matched e-class
- `substs`: vector of `Subst`, one per successful match

### `Pattern<Op>`

`Pattern<Op>` wraps a `PatternAst<Op>` and provides search/matching.

Construction:

- From a `PatternAst<Op>` directly (validates that Var nodes are leaves)
- From a plain `RecExpr<Op>` via `Pattern::fromExpr()` (no variables)

API:

- `vars()`: returns the deduplicated list of variables in the pattern
- `ast()`: returns the underlying `PatternAst`
- `search(egraph)`: find all matches across the entire e-graph
- `searchEclass(egraph, id)`: find all matches in one e-class
- `searchEclassWithSubsts(egraph, id, initial_substs)`: find matches in one e-class starting from a set of pre-bound substitutions. Variables already bound in the initial substitutions are treated as equality constraints. This is the key enabler for multi-pattern matching.

Matching semantics:

- A `Var` node matches any e-class and binds the variable to its canonical `Id`
- If the same variable appears more than once, all occurrences must bind to the same canonical `Id`
- An `ENode` pattern matches an e-node if the operator and arity match, and all children match recursively
- The matcher searches through all enodes in an e-class for potential matches

### `Searcher<Op, Analysis, OpHash>`

`Searcher` is the abstract base class for the search side of a rewrite rule.

Mirrors egg's `Searcher` trait.

A searcher finds substitutions in an e-graph that match some condition. The canonical implementation is `PatternSearcher` (pattern matching), but users can implement custom searchers for conditional rewrites, analysis-guided matching, etc.

Virtual methods:

- `search(egraph)` (pure virtual): search the entire e-graph and return matches grouped by e-class
- `searchEclass(egraph, id)`: search a single e-class. Default: delegates to `search()` and filters
- `vars()`: return the pattern variables used. Default: empty vector

### `Applier<Op, Analysis, OpHash>`

`Applier` is the abstract base class for the apply side of a rewrite rule.

Mirrors egg's `Applier` trait.

An applier uses substitutions produced by a searcher to mutate the e-graph (typically by adding nodes and merging e-classes).

Virtual methods:

- `applyOne(egraph, eclass, subst)` (pure virtual): apply a single substitution to a matched e-class. Returns the list of new Ids added or merged
- `applyMatches(egraph, matches)`: apply all matches. Default: iterates over matches and calls `applyOne()`. Returns total number of merges
- `vars()`: return the pattern variables used. Default: empty vector

### `PatternSearcher<Op, Analysis, OpHash>`

`PatternSearcher` is a `Searcher` backed by a `Pattern<Op>`.

It wraps an existing `Pattern` and delegates to its `search()`, `searchEclass()`, and `vars()` methods.

API:

- `pattern()`: returns a const reference to the underlying `Pattern`

### `PatternApplier<Op, Analysis, OpHash>`

`PatternApplier` is an `Applier` backed by a `Pattern<Op>`.

Given a substitution, it instantiates the pattern bottom-up in the e-graph and merges the result into the matched e-class. The instantiation logic is identical to the original `Rewrite` RHS instantiation.

API:

- `pattern()`: returns a const reference to the underlying `Pattern`

Error behavior:

- `std::out_of_range` if a variable in the pattern is not found in the substitution
- `std::out_of_range` if a child index is out of range during instantiation

### `Rewrite<Op, Analysis, OpHash>`

`Rewrite` is a rewrite rule backed by a `Searcher` and an `Applier`.

There are two construction modes:

**Pattern-only (backward compatible):**

```cpp
Rewrite(name, lhs_pattern, rhs_pattern)
```

Creates a `PatternSearcher` for the LHS and a `PatternApplier` for the RHS. Variable validation (every RHS var must appear in LHS) is performed at construction time.

**Generic:**

```cpp
Rewrite(name, shared_ptr<Searcher>, shared_ptr<Applier>)
```

Accepts arbitrary `Searcher`/`Applier` implementations. No automatic variable validation is performed. Null pointers are rejected with `std::invalid_argument`.

Internally, `Rewrite` stores its `Searcher` and `Applier` via `shared_ptr` (to allow `Rewrite` to remain copyable, as required by the `Runner`).

API:

- `name()`: returns the rule name
- `searcher()`: returns a const reference to the underlying `Searcher`
- `applier()`: returns a const reference to the underlying `Applier`
- `patternSearcher()`: returns a pointer to the `PatternSearcher`, or nullptr if non-pattern
- `patternApplier()`: returns a pointer to the `PatternApplier`, or nullptr if non-pattern
- `lhs()`: returns the LHS `Pattern` (throws `std::logic_error` if non-pattern)
- `rhs()`: returns the RHS `Pattern` (throws `std::logic_error` if non-pattern)
- `search(egraph)`: delegates to `searcher().search(egraph)`
- `apply(egraph, matches)`: delegates to `applier().applyMatches(egraph, matches)`
- `run(egraph)`: convenience method that calls `search` then `apply`

The caller is responsible for calling `egraph.rebuild()` after applying rewrites.

### Cost function contract

A cost function is any type that provides:

```cpp
struct MyCostFn {
  using Cost = ...;  // must support operator<, copy

  template <typename Op>
  Cost cost(const ENode<Op>& enode,
            std::function<Cost(Id)> child_costs) const;
};
```

The cost function is called with an enode and a lookup that returns the already-computed best cost for each child e-class. The function should be monotonic: the returned cost must be >= every child cost.

### `AstSize`

`AstSize` counts total AST nodes. Cost type is `std::size_t`.

Formula: `1 + sum(child_costs)`. Uses saturating addition to avoid overflow.

### `AstDepth`

`AstDepth` counts maximum AST depth. Cost type is `std::size_t`.

Formula: `1 + max(child_costs)`.

### `Extractor<Op, Analysis, CostFn, OpHash>`

`Extractor` performs greedy bottom-up extraction to find the cheapest `RecExpr` representable in each e-class.

Construction runs a fixed-point cost computation over all e-classes. For each class, every enode is evaluated; a node only gets a cost if all of its children already have known costs. The cheapest enode per class is cached. Iteration repeats until no class improves.

API:

- `Extractor(egraph, cost_fn)`: construct and compute all costs
- `findBest(id)`: returns `std::pair<Cost, RecExpr<Op>>` for the given e-class
- `findBestCost(id)`: returns only the cost
- `findBestNode(id)`: returns the cheapest enode chosen for the class

Error behavior:

- `std::out_of_range` for invalid or unknown ids (propagated from `egraph.find()`)
- `std::runtime_error` if the e-class has no finite acyclic term (cycle-only classes)

RecExpr reconstruction deduplicates shared child classes: if two children of a node point to the same canonical e-class, the extracted `RecExpr` reuses a single node for them.

### `StopReason`

`StopReason` describes why the runner stopped.

Variants (via `StopReasonKind` enum):

- `Saturated`: no rules made progress and the scheduler agrees saturation is reached.
- `IterationLimit(n)`: hit the iteration ceiling. `limit_value` equals the configured limit (`n`).
- `NodeLimit(n)`: e-graph exceeded the e-node limit. `limit_value` equals the actual node count at the moment of violation (which is greater than the configured limit).
- `TimeLimit(secs)`: wall-clock time exceeded the time limit. `elapsed_seconds` contains the measured time.
- `Other(msg)`: a user hook returned an error. `message` contains the hook-supplied string.

API:

- `StopReason::Saturated()`, `::IterationLimit(n)`, `::NodeLimit(n)`, `::TimeLimit(secs)`, `::Other(msg)`: static factory methods
- `toString()`: human-readable description suitable for log output

Diagnostic notes:

- `Saturated` is the expected outcome for well-behaved rule sets on finite graphs.
- `IterationLimit` or `NodeLimit` indicate the e-graph grew beyond configured bounds. Consider raising the limits or using `BackoffScheduler` to suppress explosive rules.
- `Other` is only produced by user hooks; inspect the `message` field for the hook's explanation.

### `Iteration`

`Iteration` stores per-iteration data accumulated during the runner loop.

Fields:

- `egraph_nodes`: memo size at start of iteration
- `egraph_classes`: class count at start of iteration
- `applied`: `unordered_map<string, size_t>` mapping rule name to merge count. Only rules that produced at least one merge appear as keys.
- `search_time`, `apply_time`, `rebuild_time`, `total_time`: timing in seconds
- `has_stop_reason`: true only on the terminal (last) iteration
- `stop_reason`: the stop reason (valid only if `has_stop_reason` is true)

Reading rule-application data:

- To check whether a specific rule fired in a given iteration: `iter.applied.count("rule-name") > 0`.
- To compute total merges across all iterations for a rule: sum `iter.applied["rule-name"]` over `runner.iterations`.
- If `applied` is empty on every iteration and the runner stopped with `Saturated`, the e-graph was already fully saturated before the first rule search.

### `Report`

`Report` is a summary across all iterations, produced by `runner.report()`.

Fields:

- `iterations`: number of iterations (equals `runner.iterations.size()`)
- `stop_reason`: why the runner stopped (mirrors `runner.stop_reason`)
- `egraph_nodes`, `egraph_classes`: final e-graph size
- `total_time`, `search_time`, `apply_time`, `rebuild_time`: cumulative timing across all iterations

API:

- `toString()`: formatted multi-line summary. Labels used: `Stop reason`, `Iterations`, `Egraph nodes`, `Egraph classes`, `Total time`, `Search time`, `Apply time`, `Rebuild time`.

Reading a Report:

```
Runner report:
  Stop reason: Saturated
  Iterations: 3
  Egraph nodes: 12
  Egraph classes: 6
  Total time: 0.000182s
  Search time: 0.000041s
  Apply time: 0.000038s
  Rebuild time: 0.000103s
```

- If `stop_reason` is not `Saturated`, increase limits or inspect per-iteration `applied` maps to understand which rules were active.
- `Report` does not include per-rule totals; derive them from `runner.iterations[i].applied`.

### `RewriteScheduler<Op, Analysis, OpHash>`

`RewriteScheduler` is the base class for controlling which rules fire and when.

Virtual methods (all have default implementations):

- `canStop(iteration)`: return true if saturation can be declared. Default: true.
- `searchRewrite(iteration, egraph, rewrite)`: search a single rule. Default: `rewrite.search(egraph)`.
- `applyRewrite(iteration, egraph, rewrite, matches)`: apply a single rule. Default: `rewrite.apply(egraph, matches)`.

### `SimpleScheduler<Op, Analysis, OpHash>`

`SimpleScheduler` is a passthrough scheduler with no rule management.

All methods use the defaults from `RewriteScheduler`. Use this when rules are well-behaved and e-graph size is not a concern.

### `BackoffScheduler<Op, Analysis, OpHash>`

`BackoffScheduler` implements exponential backoff for explosive rules.

If a rule produces more matches than a threshold, it is "banned" for some number of iterations. Each subsequent ban doubles both the threshold and the ban duration.

Constructor:

- `BackoffScheduler(initial_match_limit = 1000, initial_ban_length = 5)`

Per-rule configuration:

- `ruleMatchLimit(name, limit)`: set per-rule match threshold
- `ruleBanLength(name, length)`: set per-rule ban duration
- `doNotBan(name)`: prevent a rule from ever being banned

Backoff algorithm:

1. If the rule is currently banned (iteration < `banned_until`), skip it.
2. Compute threshold: `match_limit * 2^times_banned`.
3. Search the rule. Count total matches.
4. If matches exceed threshold: ban the rule for `ban_length * 2^times_banned` iterations, discard matches.
5. Otherwise: allow the rule to fire.

Saturation check (`canStop`):

- If any rules are currently banned, saturation is blocked. The runner continues iterating even if no rules produce merges until all bans expire.

When to use `BackoffScheduler`:

- Rules like commutativity or associativity can match exponentially many subterms. Use `BackoffScheduler` with a low `initial_match_limit` to prevent the e-graph from blowing up.
- Use `doNotBan(name)` for critical rules that must never be suppressed.

### `Runner<Op, Analysis, OpHash>`

`Runner` is the iterative equality saturation driver.

It owns the e-graph and applies a set of rewrite rules in a loop until saturation, an iteration/node/time limit is reached, or a user hook stops the run.

Construction:

- `Runner(analysis = Analysis())`: create with an empty e-graph

Builder-style configuration:

- `withIterLimit(limit)`: set iteration limit (default: 30)
- `withNodeLimit(limit)`: set e-node limit (default: 10000)
- `withTimeLimit(seconds)`: set wall-clock time limit (default: 5.0s)
- `withScheduler(scheduler)`: set the rewrite scheduler (default: SimpleScheduler). Throws `std::invalid_argument` if null.
- `withHook(hook)`: add a hook that runs at the start of each iteration

Adding expressions:

- `addExpr(expr)`: add a `RecExpr` to the e-graph and record its root id in `roots`

Running:

- `run(rules)`: run equality saturation. Accepts `vector<Rewrite>` or `vector<const Rewrite*>`. Resets per-run state (iterations, stop reason) so the runner is reusable across multiple `run()` calls. The e-graph, roots, hooks, and configuration are preserved.

Results:

- `egraph`: the e-graph (public field)
- `iterations`: per-iteration data (public field)
- `roots`: root ids of added expressions (public field)
- `has_stop_reason` / `stop_reason`: why the runner stopped
- `report()`: produce a `Report` summary

Main loop algorithm:

1. Initial `egraph.rebuild()`.
2. Loop:
   a. Check limits (time, node, iteration). If hit, record and stop.
   b. Run hooks. If a hook returns false, record `Other` and stop.
   c. Search phase: call `scheduler.searchRewrite()` for each rule.
   d. Apply phase: call `scheduler.applyRewrite()` for each rule with matches.
   e. Rebuild phase: call `egraph.rebuild()`.
   f. Saturation check: if no rules applied, scheduler allows stop, and hooks/rebuild didn't change the e-graph, declare `Saturated`.
   g. Post-work limit check.

Debugging a run:

- Check `runner.stop_reason.kind` first. `Saturated` is expected; anything else indicates a resource constraint.
- For `IterationLimit`: inspect `runner.iterations` to see which rules were still firing in the last productive iteration.
- For `NodeLimit`: the `stop_reason.limit_value` gives the node count at violation. Lower the configured limit or switch to `BackoffScheduler`.
- To trace which rules contributed most merges, iterate `runner.iterations` and sum `iter.applied[rule_name]` per rule.
- Call `runner.report().toString()` for a human-readable one-shot summary.

### `MultiPattern<Op>`

`MultiPattern` holds N `Pattern<Op>` objects that share variables. All patterns must match simultaneously with consistent variable bindings. Results are grouped by pattern[0]'s matched e-class.

Mirrors egg's `MultiPattern`.

Construction:

- `MultiPattern(vector<Pattern<Op>>)`: requires at least 1 pattern. Throws `std::invalid_argument` if empty.

API:

- `patterns()`: returns the constituent patterns
- `vars()`: returns the deduplicated union of variables across all patterns
- `size()`: returns the number of patterns
- `search(egraph)`: find all multi-matches across the entire e-graph
- `searchEclass(egraph, id)`: find multi-matches anchored at a specific e-class for pattern[0]

Search algorithm:

1. Match pattern[0] across all e-classes (or a single e-class for `searchEclass`).
2. For each subsequent pattern[i], extend surviving substitutions by trying to match pattern[i] in every e-class with the pre-bound variables from previous patterns.
3. Substitutions that fail to extend are discarded.
4. Results are grouped by pattern[0]'s matched e-class.

The key mechanism is `Pattern::searchEclassWithSubsts()`, which matches a pattern starting from pre-bound substitutions rather than an empty one.

### `MultiPatternSearcher<Op, Analysis, OpHash>`

`MultiPatternSearcher` is a `Searcher` backed by a `MultiPattern<Op>`.

It wraps a `MultiPattern` and implements the `Searcher` interface so multi-pattern rules integrate seamlessly with `Rewrite` and `Runner`.

API:

- `multiPattern()`: returns a const reference to the underlying `MultiPattern`
- `search(egraph)`: delegates to `MultiPattern::search()`
- `searchEclass(egraph, id)`: delegates to `MultiPattern::searchEclass()`
- `vars()`: returns the union of variables from all patterns

### `JustificationKind`

`JustificationKind` is an enum with two variants:

- `Rule`: a named rewrite rule caused the merge.
- `Congruence`: the merge was due to congruence (children became equal).

### `Justification`

`Justification` records why two e-nodes were merged.

Fields:

- `kind`: a `JustificationKind` value
- `rule_name`: the rule name (only meaningful when `kind == Rule`)

Static factories:

- `Justification::Rule(name)`: create a rule justification
- `Justification::Congruence()`: create a congruence justification

API:

- `isRule()`: true if `kind == Rule`
- `isCongruence()`: true if `kind == Congruence`

### `Connection`

`Connection` is a directed edge in the proof graph between two e-nodes.

Fields:

- `next`: the target e-node Id
- `current`: the source e-node Id
- `justification`: why the edge exists
- `is_rewrite_forward`: direction of the rewrite

### `ExplainNode`

`ExplainNode` is one node in the proof forest.

Fields:

- `neighbors`: all connections (tree edges + shortcut edges)
- `parent_connection`: the tree edge to this node's parent in the proof forest

### `Explain<Op, OpHash>`

`Explain` is the proof forest that tracks justifications. It is a separate data structure from the EGraph. The EGraph holds an `Explain` instance and delegates to it during add/merge/rebuild.

Mirrors egg's `Explain<L>`.

Key operations (called by EGraph):

- `addNode(id)`: register a new e-node in the proof forest
- `recordUnion(node1, node2, justification)`: record a union in the proof forest
- `alternateRewrite(node1, node2, justification)`: record a shortcut edge when nodes are already equivalent but a new proof path is discovered

Proof reconstruction:

- `getPathUnoptimized(left, right)`: returns the path of connections from left to right through the proof forest

Internal operations:

- `makeLeader(node)`: reverses parent pointers to make a node the root of its tree
- `commonAncestor(left, right)`: finds the lowest common ancestor in the proof forest

### `TreeTerm<Op>`

`TreeTerm` is a compact proof step in tree form.

Each TreeTerm represents one intermediate term in the proof. A rewrite step is annotated with `forward_rule` or `backward_rule`. For congruence steps, `child_proofs` contains sub-proofs showing how each child was transformed.

Fields:

- `node`: the `ENode<Op>` at this proof step
- `backward_rule`: rule name rewriting this back to the previous term (empty if none)
- `forward_rule`: rule name rewriting the previous term to this one (empty if none)
- `child_proofs`: one sub-proof (vector of shared TreeTerm pointers) per child position

### `TreeExplanation<Op>`

Type alias for `std::vector<std::shared_ptr<TreeTerm<Op>>>`. A tree explanation is a sequence of tree proof steps.

### `FlatTerm<Op>`

`FlatTerm` is a fully expanded term in a flat proof.

In a `FlatExplanation`, each consecutive pair of FlatTerms differs by exactly one rewrite step.

Fields:

- `node`: the `ENode<Op>` at this step
- `backward_rule`, `forward_rule`: rule annotations
- `children`: one `FlatTerm` per child position (recursive tree structure)

API:

- `hasRewriteForward()`: recursively checks if any node has a forward rule
- `hasRewriteBackward()`: recursively checks if any node has a backward rule
- `removeRewrites()`: strips all rule annotations recursively
- `getRecExpr()`: converts the flat term tree to a `RecExpr<Op>`

### `FlatExplanation<Op>`

Type alias for `std::vector<FlatTerm<Op>>`. A flat explanation is a sequence of flat terms.

### `Explanation<Op>`

`Explanation` is the user-facing result of explaining an equivalence.

Contains a tree representation (compact, shared sub-proofs) and lazily computes a flat representation (one rewrite per step).

Mirrors egg's `Explanation<L>`.

API:

- `trees()`: returns the `TreeExplanation` (sequence of tree proof steps)
- `flatExplanation()`: returns the `FlatExplanation` (lazily computed on first access)
- `treeSize()`: number of proof steps in tree form

### `explainIdEquivalence(egraph, left, right)`

Free function (in `explain.h`) that explains why two e-class Ids are equivalent.

Returns an `Explanation<Op>` containing the full proof.

Error behavior:

- `std::logic_error` if explanations are not enabled
- `std::invalid_argument` if the ids are not equivalent

### `explainEquivalence(egraph, left_expr, right_expr)`

Free function (in `explain.h`) that explains why two `RecExpr<Op>` expressions are equivalent.

Looks up each expression in the e-graph, then explains the equivalence of the resulting Ids.

Error behavior:

- `std::logic_error` if explanations are not enabled
- `std::invalid_argument` if the expressions are not found or not equivalent

## Explanation usage guide

### Enabling explanation recording

Explanation recording is opt-in.  Call `withExplanationsEnabled()` on a fresh
`EGraph` **before** adding any nodes.  Calling it after nodes have been added
throws `std::logic_error`.

```cpp
EGraph<MyOp> egraph;
egraph.withExplanationsEnabled();   // must be first

Id a = egraph.add(make_node("a"));
Id b = egraph.add(make_node("b"));
egraph.unionTrusted(a, b, "my-rule");
egraph.rebuild();
```

### Using the free functions

Both free functions live in `explain.h` and take a **non-const** reference to
the `EGraph`:

```cpp
#include "nndeploy/egraph/explain.h"

// Explain two Ids.
Explanation<MyOp> expl =
    explainIdEquivalence(egraph, id_a, id_b);

// Explain two RecExprs (looks up their Ids first).
Explanation<MyOp> expl2 =
    explainEquivalence(egraph, expr_a, expr_b);
```

### When free functions throw

| Function | Throws | Condition |
|---|---|---|
| `explainIdEquivalence` | `std::logic_error` | explanations not enabled |
| `explainIdEquivalence` | `std::invalid_argument` | ids are not equivalent |
| `explainEquivalence` | `std::logic_error` | explanations not enabled |
| `explainEquivalence` | `std::invalid_argument` | expression not found in e-graph, or expressions are not equivalent |

### Reading an Explanation

```cpp
// Tree form (compact, shared sub-proofs):
const TreeExplanation<MyOp>& trees = expl.trees();
for (const auto& step : trees) {
  if (step->hasForwardRule())  { /* step->forward_rule */ }
  if (step->hasBackwardRule()) { /* step->backward_rule */ }
  // Congruence steps have non-empty child_proofs.
}

// Flat form (one rewrite per consecutive pair, lazily computed):
const FlatExplanation<MyOp>& flat = expl.flatExplanation();
for (const auto& ft : flat) {
  if (ft.hasRewriteForward() || ft.hasRewriteBackward()) { /* annotated step */ }
  RecExpr<MyOp> term = ft.getRecExpr();  // recover the term
}
```

### Constraints and caveats

- `withExplanationsEnabled()` must be called on an **empty** e-graph.  There
  is no way to enable explanation recording retroactively.
- With explanations enabled, `add()` allocates an extra syntactic `Id` for
  every duplicate node insertion (to preserve syntactic identity for proofs).
  This increases memory usage compared to the non-explanation path.
- `lookupRecExpr()` uses the uncanonical memo when explanations are enabled,
  and falls back to the canonical memo otherwise.  Both paths throw
  `std::invalid_argument` if any node in the expression is not found.
- `egraph.explain()` is a low-level accessor for the proof forest.  For most
  use cases, `explainIdEquivalence` and `explainEquivalence` are sufficient.
- `Runner` supports explanations: call `runner.egraph.withExplanationsEnabled()`
  before calling `runner.addExpr()`.

## Analysis contract

`Analysis` must provide:

```cpp
struct Analysis {
  struct Data { ... };

  Data make(const ENode<Op>& node) const;
  void merge(Data& dst, const Data& src) const;
};
```

Required behavior:

- `make()` initializes data for a newly inserted enode.
- `merge()` combines class data when two classes are unified.

Recommended behavior:

- `merge()` should be associative and commutative.
- `Data` should remain valid after repeated merges during rebuild.

## Operational semantics

### `add(node)`

`add()` inserts an enode after canonicalizing its children.

Behavior:

1. Canonicalize child ids with `find()`.
2. Look up the canonicalized enode in the memo table.
3. Reuse the existing class if the enode already exists.
4. Otherwise create a new class and store the enode.
5. Register parent links from each child class back to the new class.

When explanations are enabled (`withExplanationsEnabled()`), `add()` behaves differently for duplicate nodes:

6. A new Id is allocated for the exact syntactic form.
7. The new Id is stored in the node storage and uncanonical memo.
8. The new Id is merged with the existing one via congruence justification.

This preserves the exact syntactic form for proof reconstruction.

Result:

- structurally identical enodes map to the same e-class

### `addExpr(expr)` (RecExpr overload)

`addExpr(const RecExpr<Op>&)` adds a complete expression bottom-up into the e-graph.

Behavior:

1. Iterate the RecExpr in topological order.
2. For each node, remap child Ids from RecExpr indices to live e-graph Ids.
3. Call `add()` for each remapped node.
4. Return the Id of the root (last node).

This is used internally by rewrite RHS instantiation.

### `merge(lhs, rhs)`

`merge()` unifies two classes.

Behavior:

1. Canonicalize both ids.
2. Union them in `UnionFind`.
3. Move nodes and parent links into the surviving class.
4. Merge analysis data.
5. Mark the surviving root dirty.

Important:

- `merge()` alone does not restore congruence among parent nodes.
- `rebuild()` must be called after one or more merges.
- When explanations are enabled, `merge()` delegates to `mergeWithJustification()` which records the justification in the proof forest before performing the union.
- `unionTrusted(lhs, rhs, rule_name)` is a convenience for merging with a named rule justification.

### `rebuild()`

`rebuild()` restores congruence closure.

Behavior:

1. Re-canonicalize every node’s child ids.
2. Rebuild the memo table from canonical nodes.
3. If two canonicalized parent nodes become identical, unify their classes.
4. Repeat until no new class merges occur.
5. Rebuild parent links from scratch.

When explanations are enabled, congruence-induced merges during rebuild are recorded with `Congruence` justification in the proof forest.

Current model:

- `rebuild()` is global, not incremental
- `dirty_roots_` is used as a trigger, not as a fine-grained worklist

This is correct for the current scope, but it is simpler than `egg`’s more mature incremental machinery.

## Invariants

After a completed `rebuild()`, the module is expected to satisfy these invariants:

1. Every live class id is canonical under `UnionFind`.
2. Every enode stored in `classes_` has canonical child ids.
3. The memo table maps each canonical enode to exactly one canonical class id.
4. If two parent enodes become structurally equal after child merges, they belong to the same e-class.
5. Parent links reflect the rebuilt class graph.

## Complexity expectations

For the current implementation:

- `find()` is near-constant amortized time.
- `merge()` is near-constant amortized time plus vector concatenation cost.
- `add()` is hash lookup plus child canonicalization.
- `rebuild()` is proportional to the number of classes and stored enodes, and may repeat until no new parent congruences are discovered.

This implementation prioritizes correctness and simplicity over incremental performance.

## Current limitations

This module does **not** yet implement the full `egg` stack.

Missing pieces include:

- **Incremental rebuild optimization**: While `rebuild()` is correct, the transition to a high-performance incremental model is deferred this quarter.
- **S-expression parser for patterns**: Broad support for string-based pattern/expression parsing is deferred this quarter. Patterns must be constructed via `PatternAst` or `RecExpr`.

Invalid or foreign `Id`s are treated as programming errors and throw `std::out_of_range`.

## Test coverage

The current tests cover:

- deduplication of equivalent leaf nodes
- merge + rebuild congruence on direct parents
- nested congruence propagation through repeated rebuild merging
- analysis merge behavior
- parent tracking after rebuild
- merge idempotence
- rebuild idempotence
- add-after-merge-before-rebuild canonicalization
- invalid-id safety behavior
- RecExpr topological ordering and forward-reference rejection
- RecExprBuilder ergonomics and validation
- Var symbolic and numeric parsing
- Subst insert and lookup
- variable-only pattern, repeated-var deduplication, nested operator patterns
- variable-with-children rejection
- pattern conversion from RecExpr
- whole-graph and single-eclass pattern matching
- repeated-variable equality constraint matching
- no-match case
- search after merge with congruence
- RHS unbound variable rejection
- additive identity rewrite `(+ ?a 0) => ?a`
- commutativity rewrite `(+ ?a ?b) => (+ ?b ?a)`
- idempotent repeated rewrite application
- addExpr from RecExpr
- simple leaf extraction
- choosing smaller term from equivalent class
- extraction after rewrite `(+ ?a 0) => ?a`
- nested RecExpr reconstruction
- findBestCost and findBestNode accessors
- AstDepth cost function
- shared child deduplication in extracted RecExpr
- choosing best among multiple equivalents after merge
- invalid-id rejection in extractor
- runner saturation with additive identity
- runner iteration limit stops execution
- runner node limit stops execution
- runner report contains correct summary data
- runner iteration data tracks applied rules per iteration
- runner hook can stop execution with custom message
- runner with multiple rules saturates (commutativity + additive identity)
- runner with empty rules saturates immediately
- runner roots are tracked across addExpr calls
- backoff scheduler bans explosive rules
- backoff scheduler ban duration is exact (rule skipped for configured iterations)
- simple scheduler passthrough behavior
- StopReason toString covers all variants
- hook mutation prevents premature saturation declaration
- PatternSearcher used directly (outside Rewrite)
- PatternApplier used directly (outside Rewrite)
- custom Searcher finds leaf nodes by operator name
- custom Applier wraps matched nodes with a new operator
- generic Rewrite constructor with custom Searcher/Applier
- null Searcher rejected in generic constructor
- null Applier rejected in generic constructor
- generic Rewrite search and apply end-to-end
- generic Rewrite run through Runner with conditional applier
- pattern-based Rewrite lhs()/rhs() accessors work correctly
- mixed pattern-based and custom rules in a single Runner
- custom Applier applyMatches default iterates over all substitutions
- Searcher searchEclass default delegates to search and filters
- runner is reusable (second run() resets state correctly)
- withScheduler(nullptr) is rejected with std::invalid_argument
- report() before run() returns a valid Report with well-defined defaults
- MultiPattern rejects empty pattern list
- single-pattern MultiPattern delegates to underlying Pattern
- two patterns with shared variables produce consistent bindings
- no match when second pattern has no matching operator
- shared variables constrain bindings across patterns
- multi-pattern match after merge with congruence
- three patterns chained with shared variables
- disjoint variables across patterns produce cartesian combinations
- searchEclass anchors first pattern to a specific e-class
- MultiPatternSearcher integrates with Rewrite via generic constructor
- vars() returns deduplicated union across all patterns
- MultiPatternSearcher runs through Runner end-to-end
- enabling explanations after adding nodes throws std::logic_error
- explain() without enabling throws std::logic_error
- trivial same-node explanation (node equal to itself)
- direct merge with named rule produces proof containing the rule name
- direct merge rule name present when explaining in forward direction
- direct merge rule name present when explaining in reverse direction
- congruence merge explanation (f(a)==f(b) after merging a==b)
- congruence step child_proofs have populated content
- explanation after rewrite rule application (+ a 0) => a
- proof direction is recorded (forward/backward)
- flat explanation conversion from tree form
- flat proof preserves rule annotations through flattening
- FlatTerm removeRewrites() strips all annotations recursively
- FlatTerm hasRewriteForward / hasRewriteBackward directional helpers
- FlatTerm hasRewriteForward propagates recursively into children
- FlatTerm::getRecExpr() conversion to RecExpr (leaf and composite)
- explaining non-equivalent ids throws std::invalid_argument
- explaining non-equivalent pairs: partial equivalence correctly partitioned
- explainEquivalence via RecExpr free function
- explainEquivalence throws when expression not in e-graph
- alternate rewrite for already-equal nodes (shortcut edge)
- alternate rewrite: original rule still visible after shortcut recorded
- multi-step transitive proof (a==b==c via two rules)
- four-step transitive proof (a==b==c==d, all three rules visible)
- trivial self-explanation flat form has no rule annotations
- explain after Runner saturation still produces valid proofs
- explanationsEnabled() accessor
- node storage grows with add() calls
- duplicate add creates new Id when explanations enabled
- existing e-graph operations work correctly with explanations enabled

The test target is:

- `egraph_test`

## Next-Quarter Decision Package

Neither the parser nor incremental rebuild is being implemented this quarter; this section is only the Q3 2026 prioritization rule.

- **Make parser work must-ship first** when the main adoption pain is still authoring friction. This quarter's ergonomics work (`RecExprBuilder`, `PatternBuilder`) removed index bookkeeping, but `RecExpr`/`Pattern` construction in `recexpr.h`, the cookbook smoke cases `CookbookWorkflowTest.StructuralExpressionConstruction` and `CookbookWorkflowTest.StructuralPatternAuthoring`, and the public adoption smoke tests still require explicit topological node-by-node C++ assembly. If next quarter's consumers are blocked more by verbose workflow authoring than by runtime cost, parser-first is the right call.
- **Prioritize incremental rebuild first** when scale or resilience data says rebuild cost is the dominant blocker. `EGraph::rebuild()` in `egraph.h` is still global, `dirty_roots_` is only a trigger, and `Runner::run()` performs an initial rebuild plus another rebuild after every iteration. If larger smoke/resilience runs start hitting `NodeLimit`/`TimeLimit`, or Runner reports show rebuild time dominating search/apply time, incremental rebuild should move ahead of parser work.
- **Tie-breaker recommendation: parser-first.** This quarter's evidence concentrated on usability and adoption hardening (cookbook workflows, API smoke tests, explanation guidance), and those paths are correct but still verbose. Until quarter evidence shows rebuild cost breaking real workflows, treat parser work as the default next-quarter priority.

## Intended next steps (Future Quarters)

Suggested next layers for future development:

1. Transition from global fixed-point `rebuild()` to a more incremental model.
2. Comprehensive S-expression parser for patterns and expressions.
3. Performance parity with the Rust `egg` library for larger-scale graphs.

## TODO list (Future Quarters)

The future development roadmap includes:

1. Improve rebuild from global fixed-point recomputation toward a more incremental model.
2. Add comprehensive s-expression parser for patterns and expressions.
3. Register `egraph_test` with automated CTest discovery for all build variants.
