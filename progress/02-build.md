# Checkpoint 2 — Build engine + benchmark ✅

**Status:** PASS

## Commands

```bash
mingw32-make minichess    # -> build/minichess-ubgi.exe
mingw32-make benchmark    # -> build/minichess-benchmark.exe
```

## Result

- Both compiled with `--std=c++2a -Wall -Wextra -Wpedantic -O3 -march=native`.
- Exit code 0, **no warnings or errors**.
- Artifacts produced:
  - `build/minichess-ubgi.exe` (~2.7 MB)
  - `build/minichess-benchmark.exe`

## Confirms

- TODO group 4 (root `search()`) and group 3 (`eval_ctx()`) compile and link —
  the README's "build-stopping error" no longer applies; the code is complete.
- `state.cpp` (evaluate + naive knight moves) compiles alongside the policy
  sources.
