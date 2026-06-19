import subprocess
from cli import cli

WHITE="build/minichess-ubgi.exe"
BOSS="build/boss-ubgi.exe"
procs={}

def persistent_get_engine_move(engine_path, algo, params, uci_moves, time_limit, depth=0):
    key=(engine_path, algo)
    p=procs.get(key)
    if p is None or p.poll() is not None:
        p=subprocess.Popen([engine_path],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
        procs[key]=p
        def s(c): p.stdin.write((c+"\n").encode()); p.stdin.flush()
        s("ubgi"); s(f"setoption name Algorithm value {algo}")
        for pr in params or []:
            if "=" in pr: k,v=pr.split("=",1); s(f"setoption name {k} value {v}")
        s("isready")
        while True:
            r=p.stdout.readline()
            if not r or r.decode(errors="replace").strip() in ("ubgiok","readyok","uciok"): break
    def s(c): p.stdin.write((c+"\n").encode()); p.stdin.flush()
    s("position startpos"+(" moves "+" ".join(uci_moves) if uci_moves else ""))
    s(f"go movetime {time_limit}")
    bestmove=None; last_info=None
    while True:
        r=p.stdout.readline()
        if not r: break
        line=r.decode(errors="replace").strip()
        if line.startswith("info ") and "depth" in line: last_info=cli._parse_info(line)
        elif line.startswith("bestmove"):
            parts=line.split(); bestmove=parts[1] if len(parts)>=2 else None; break
    return bestmove, last_info

cli._init_game("minichess")
cli.get_engine_move = persistent_get_engine_move

results=[]
for g in range(4):
    w,b=(WHITE,BOSS) if g%2==0 else (BOSS,WHITE)
    res=cli.run_game(w,b,2000,"pvs","pvs",verbose=False,game_num=g+1,total_games=4)
    # normalize to our-engine perspective
    if g%2==0: ours = "win" if res=="white" else ("loss" if res=="black" else "draw")
    else:      ours = "win" if res=="black" else ("loss" if res=="white" else "draw")
    results.append(ours); print(f"game {g+1}: raw={res} ours={ours}")
for p in procs.values():
    try: p.kill()
    except: pass
print("REUSED-PROCESS RESULT (our pvs vs boss):", results.count("win"),"W",results.count("loss"),"L",results.count("draw"),"D")
