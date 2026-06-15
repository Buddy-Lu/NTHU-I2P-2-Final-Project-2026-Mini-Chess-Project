import sys, time
sys.path.insert(0, "gui")
import config as _c
from ubgi_client import UBGIEngine, discover_engines

engines = discover_engines(_c.BUILD_DIR)
print("DISCOVERED:", engines)

# best for minichess
def best(game="minichess"):
    for name, path in engines:
        if name.lower().startswith(game):
            return path
    return engines[0][1] if engines else None

p = best()
print("ANALYZE ENGINE:", p)
eng = UBGIEngine(p, initial_options={"Algorithm":"minimax"})
print("game_name:", eng.game_name, "alive:", eng.is_alive())

infos = []
eng.set_position()
eng.go(infinite=True, info_callback=lambda d: infos.append(d), done_callback=lambda b: print("DONE bestmove:", b))
time.sleep(2.0)
eng.stop()
time.sleep(0.3)
print("INFO LINES RECEIVED:", len(infos))
if infos:
    print("LAST INFO:", infos[-1])
eng.quit()
