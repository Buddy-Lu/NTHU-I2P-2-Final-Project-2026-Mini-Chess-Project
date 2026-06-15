# Checkpoint 1 — Environment ✅

**Status:** PASS

## What I checked

| Tool | Result |
|------|--------|
| `g++` | `g++.exe (Rev3, MSYS2) 13.2.0` at `/c/msys64/ucrt64/bin/g++` — C++20 (`--std=c++2a`) OK |
| `make` | `mingw32-make` — GNU Make 4.4.1 |
| Python | `.venv/Scripts/python.exe` → Python 3.12.2 |
| pygame | `pygame-ce 2.5.7` (SDL 2.32.10) installed in venv |
| micromamba | `micromamba.exe`, base env active (`MAMBA_ROOT_PREFIX=C:\Users\Buddy\micromamba`) — provides the environment plumbing |

## Notes

- `micromamba` supplies the runtime environment ("without it shit can't work").
  The compiler itself comes from MSYS2 UCRT64, and Make invokes a POSIX `sh` so
  the Makefile's `mkdir -p` / `rm -f` recipes run correctly.
- No changes needed — environment is ready.
