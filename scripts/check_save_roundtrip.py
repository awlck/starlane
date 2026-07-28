#!/usr/bin/env python3
"""Save/restore round-trip check: no baseline needed.

Play some turns, save (A), play more turns, restore A, save again (B).
A and B must describe the same state, or restore lost or kept something it shouldn't.
"""
import subprocess, pathlib, os, zlib, sys

SP = os.environ["SP"]
BIN = sys.argv[1] if len(sys.argv) > 1 else "cmake-build-relwithdebinfo/starlane-console/starlane-console"
PLAY = ["look", "x me", "take all", "north", "wait", "open door", "i", "search"]
MORE = ["drop all", "south", "east", "wait", "z", "take all", "look"]

def load(p):
    return zlib.decompressobj().decompress(open(p, "rb").read()).decode("utf-8", "replace")

ok = bad = skipped = 0
for g in sorted(pathlib.Path("testdata").rglob("*.taf")):
    a, b = f"{SP}/rt_a.sav", f"{SP}/rt_b.sav"
    for f in (a, b):
        if os.path.exists(f): os.remove(f)
    cmds = ("\n".join(PLAY) + f"\nsave\n{a}\n" + "\n".join(MORE)
            + f"\nrestore\n{a}\nsave\n{b}\nquit\ny\n")
    try:
        p = subprocess.run([BIN, "--seed", "1", str(g)], input=cmds,
                           capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        print("TIMEOUT", g); bad += 1; continue
    if not (os.path.exists(a) and os.path.exists(b)):
        skipped += 1  # game never reached a save prompt (intro gates, etc.)
        continue
    if "Restored." not in p.stdout:
        print("RESTORE DID NOT SUCCEED:", g); bad += 1; continue
    if load(a) == load(b):
        ok += 1
    else:
        bad += 1
        print("ROUND TRIP DIFFERS:", g)
        ta, tb = load(a).split("\n"), load(b).split("\n")
        import difflib
        print("\n".join(list(difflib.unified_diff(ta, tb, lineterm="", n=0))[:12]))
print(f"{ok} round-trip clean, {bad} broken, {skipped} never reached a save prompt")
