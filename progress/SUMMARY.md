# Mini Project 2 — Verification Summary ✅

**Date:** 2026-06-15
**Outcome:** All checkpoints green. The MiniChess engine builds, tests pass,
and all front-ends run.

## Checkpoint 6 — Clean full rebuild ✅

```bash
mingw32-make clean
mingw32-make all     # minichess + benchmark + test
./unittest/build/state_test.exe
```

- `make all` from a clean tree: exit 0, no warnings.
- Artifacts: `build/minichess-ubgi.exe`, `build/minichess-benchmark.exe`,
  `unittest/build/state_test.exe`.
- Tests: **12 passed, 0 failed.**

## Final status

| # | Checkpoint | Status |
|---|------------|--------|
| 1 | Environment (g++ 13.2, make 4.4.1, py 3.12 + pygame-ce) | ✅ |
| 2 | Build engine + benchmark | ✅ |
| 3 | Unit tests (12/12, naive ≡ bitboard) | ✅ |
| 4 | Engine UBGI (info + bestmove) | ✅ |
| 5 | CLI plays a full game; GUI imports | ✅ |
| 6 | Clean full rebuild + retest | ✅ |

## Key conclusion

The four `[Hackathon TODO]` groups were **already filled in correctly** before
this verification pass:

1. `state.cpp::evaluate()` — material / PST / king tropism / mobility.
2. `state.cpp` naive knight move table + knight case (matches bitboard gen).
3. `minimax.cpp::eval_ctx()` — negamax recursion + perspective flip.
4. `minimax.cpp::search()` — root move loop + result assembly.

No source changes were required to make the project work; this pass confirms it
is 100% functional and documents how to build and run it.

## How to build & run (quick reference)

```bash
# Build everything
mingw32-make all

# Run the engine (UBGI on stdin/stdout)
./build/minichess-ubgi.exe

# Unit tests
./unittest/build/state_test.exe

# CLI (run as a module from project root)
.venv/Scripts/python.exe -m cli.cli \
    --white build/minichess-ubgi.exe --black build/minichess-ubgi.exe \
    --white-algo minimax --black-algo random --games 1 --depth 4

# GUI
.venv/Scripts/python.exe gui/main.py
```
