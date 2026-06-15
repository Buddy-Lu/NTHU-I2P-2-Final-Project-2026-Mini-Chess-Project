# Checkpoint 5 — CLI + GUI front-ends ✅

**Status:** PASS

## CLI

```bash
# Must run as a module from the project root:
python -m cli.cli --white build/minichess-ubgi.exe \
                  --black build/minichess-ubgi.exe \
                  --white-algo minimax --black-algo random \
                  --games 1 --depth 4
```

- Played a complete game; board rendered each ply with eval/nodes/nps.
- Terminated correctly: `>> White wins!` / `Result: 1-0`, exit 0.

### Caveat (documented, not a code change)

- `python cli/cli.py ...` (the README form) fails with
  `ModuleNotFoundError: No module named 'cli.games'` because `cli.py` does
  `from cli.games.minichess import ...` at top level with **no** import fallback.
- **Workaround:** invoke as a module — `python -m cli.cli ...` — from the
  project root. This is the reliable invocation.

## GUI

```bash
SDL_VIDEODRIVER=dummy python -c "import gui.main"
```

- `gui.main` imports cleanly (`GUI imports OK`); pygame-ce loads.
- The GUI is interactive (`python gui/main.py`); it was verified to import and
  initialize without error rather than driven headless.
