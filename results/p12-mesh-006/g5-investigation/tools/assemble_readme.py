#!/usr/bin/env python3
"""G5 investigation: insert the generated tables (data/analysis.md, data/analysis.json) into README.md
in place of its ⟨SECTIONn⟩ placeholders. Tables are copied verbatim, never retyped."""
import json
import pathlib

HERE = pathlib.Path(__file__).resolve().parents[1]
md = (HERE / "data" / "analysis.md").read_text()
an = json.loads((HERE / "data" / "analysis.json").read_text())
readme = (HERE / "README.md").read_text()


def block(title):
    """The table(s) under the '### title' heading, up to the next '### '."""
    start = md.index("### " + title)
    body = md[start:].split("\n", 1)[1]
    end = body.find("\n### ")
    return (body if end < 0 else body[:end]).strip() + "\n"


def discrete_table():
    rows = ["| n | L∞ point | RMS point | L1 point | L∞ cell-average | RMS cell-average | \\|G err\\|/G | \\|u_max err\\|/u_max | u_axis | G |",
            "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    for n, r in an["discrete"].items():
        rows.append(f"| {n} | {r['point']['linf']:.5e} | {r['point']['rms']:.5e} | {r['point']['l1']:.5e} | "
                    f"{r['average']['linf']:.5e} | {r['average']['rms']:.5e} | {r['G_rel_error']:.5e} | "
                    f"{r['u_axis_rel_error']:.5e} | {r['u_axis']:.8f} | {r['G']:.8f} |")
    return "\n".join(rows) + "\n"


subs = {
    "⟨SECTION3⟩": block("Norm cross-check"),
    "⟨SECTION4⟩": block("CFDApp vs analytical, discrete vs analytical, CFDApp vs discrete"),
    "⟨SECTION5⟩": "Exact solution of the discrete equations (`tools/duct_discrete.py`) vs the analytical truth. "
                  "The eigen-decomposition and a sparse direct solve agree to ≤ 7e-14 (n ≤ 64, logs/discrete.log).\n\n"
                  + discrete_table(),
    "⟨SECTION6⟩": block("Observed order"),
    "⟨SECTION7⟩": block("Richardson / GCI") + "\n### Required resolution\n\n" + block("Resolution required"),
    "⟨SECTION9_TABLES⟩": block("Directional symmetry, n16"),
    "⟨SECTION10_2⟩": block("Implementation-audit experiments"),
}
for key, text in subs.items():
    if key not in readme:
        raise SystemExit(f"placeholder {key} not found")
    readme = readme.replace(key, text.rstrip("\n"))
(HERE / "README.md").write_text(readme)
left = [p for p in ("⟨SECTION1", "⟨SECTION3", "⟨SECTION4", "⟨SECTION5", "⟨SECTION6", "⟨SECTION7", "⟨SECTION8",
                    "⟨SECTION9", "⟨SECTION10", "⟨SECTION11", "⟨SECTION12", "⟨SECTION13", "⟨SECTION15") if p in readme]
print("remaining placeholders:", left)
