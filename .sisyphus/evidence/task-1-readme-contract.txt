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

### Deferred This Quarter
- **S-expression Parser**: Broad support for string-based pattern/expression parsing is deferred. Patterns must be constructed via `PatternAst` or `RecExpr`.
- **Incremental Rebuild Redesign**: While `rebuild()` is correct, the transition to a high-performance incremental model is deferred.
- **Performance Parity Chase**: Matching the raw throughput of the Rust `egg` library is not a goal this quarter. Correctness and usability take precedence.

### Breaking-Change Policy
We commit to **no silent public API breakage**. Any unavoidable breaking changes to the public API must be:
1. Documented in this README with a clear rationale.
2. Accompanied by a migration path for internal consumers.
3. Explicitly called out in the release notes.

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
- `Pattern<Op>`
- `SearchMatches<Op>`
- `Searcher<Op, Analysis, OpHash>`
- `Applier<Op, Analysis, OpHash>`
- `PatternSearcher<Op, Analysis, OpHash>`
- `PatternApplier<Op, Analysis, OpHash>`
- `Rewrite<Op, Analysis, OpHash>`
- `AstSize`
- `AstDepth`
