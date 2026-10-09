import subprocess, sys, collections
diff = subprocess.run(["git","diff","--name-only","--","results/validation"],
                      capture_output=True, text=True).stdout.split()
seen = collections.Counter()
for f in diff:
    if not f.endswith(".md"): continue
    try: lines = open(f, encoding="utf-8").read().splitlines()
    except OSError: continue
    for ln in lines:
        if ln.strip().startswith("|") and "---" not in ln:
            cells = [c.strip() for c in ln.split("|")]
            for idx in (5, 7, 9):
                if idx < len(cells) and cells[idx]:
                    seen[(len(cells), idx, cells[idx])] += 1
            break
for (ncols, idx, header), n in sorted(seen.items()):
    print(f"  table width {ncols:2d}  column {idx}  header = {header!r}   ({n} files)")
