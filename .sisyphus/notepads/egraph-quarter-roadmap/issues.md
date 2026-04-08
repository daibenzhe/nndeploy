# Issues / Gotchas

## [2026-04-08] Session Start

### Build
- Build directory was empty — need to run cmake configure first: `cmake -DENABLE_NNDEPLOY_TEST=ON -DENABLE_NNDEPLOY_OPENCV=OFF -B build .`
- After cmake configure, `make egraph_test -j4` works fine
- The `make egraph_test` from the plan description works AFTER cmake configure has been done

### Important Notes
- The egraph headers are header-only (all in .h files) — no separate .cc source files for egraph
- Test file is a single large file (2221 lines) — be careful not to lose existing tests
