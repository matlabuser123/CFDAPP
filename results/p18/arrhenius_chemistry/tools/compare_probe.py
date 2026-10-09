#!/usr/bin/env python3
"""P18.5 -- compare the probe's measured values against the independent mpmath reference.

G6 criterion (a) requires omega_i checked against an INDEPENDENT arbitrary-precision
implementation at a relative error <= 32 * eps. The gtest suite does that against literals
generated from the reference. THIS does it the other way round, with no literals at all:

    arrhenius_probe (CFDApp, double)  --prints-->  KT / MATRIX lines at 17 sig figs
    compare_probe.py (mpmath, dps=60) --reads-->   recomputes and reports the error in eps

so the one failure mode a literal table cannot catch -- a wrong digit in the table itself -- is
not reachable here. The two routes are redundant on purpose: if they ever disagree, one of the two
transcription paths is broken and the disagreement says which.

    bash tools/run_probe.sh > logs/04_probe.log
    python3 tools/compare_probe.py logs/04_probe.log

THE THRESHOLD IS G6's, inherited verbatim and NOT recomputed here: 32 * eps relative.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mpmath import mp, mpf  # noqa: E402

import arrhenius_reference as ref  # noqa: E402

mp.dps = 60

EPS = mpf(2) ** -52  # 2.220446049250313e-16, double precision
LIMIT = 32  # G6's allowance, in units of eps


def rel_eps(measured, exact):
    """Relative error in units of eps. Exact zero is handled by the caller."""
    return abs(mpf(measured) - exact) / abs(exact) / EPS


def main():
    if len(sys.argv) != 2:
        print("usage: compare_probe.py <probe log>")
        return 2
    with open(sys.argv[1], "r") as handle:
        lines = handle.read().splitlines()

    worst = {}
    counts = {"KT": 0, "MATRIX": 0, "MATRIX_ZERO": 0}
    failures = []

    def record(kind, value, where):
        if kind not in worst or value > worst[kind][0]:
            worst[kind] = (value, where)
        if value > LIMIT:
            failures.append(f"{kind} {where}: {float(value):.2f} eps > {LIMIT} eps")

    def numeric(fields):
        """True only if every field parses as a number.

        THE COLUMN HEADER OF THE MATRIX BLOCK BEGINS WITH THE WORD "MATRIX" TOO, and the first
        version of this comparator tried to parse it as data and crashed. A crash is the benign
        outcome; the dangerous one would have been silently skipping real rows, so every candidate
        row is now validated and an unparseable row that is NOT the known header is reported
        rather than ignored.
        """
        try:
            for item in fields:
                float(item)
            return True
        except ValueError:
            return False

    rows = 0
    skipped = []
    for line in lines:
        f = line.split()
        if not f:
            continue

        if f[0] in ("KT", "MATRIX") and not numeric(f[1:]):
            skipped.append(line.strip())
            continue

        if f[0] == "KT":
            t = mpf(f[1])
            record("k(T)", rel_eps(f[2], ref.rate_coefficient(ref.A, ref.N, ref.EA, t)), f"T={f[1]}")
            record("x(T)", rel_eps(f[3], -ref.EA / (ref.R_U * t)), f"T={f[1]}")
            counts["KT"] += 1

        elif f[0] == "MATRIX":
            y_h2, y_o2, p, t = (mpf(f[1]), mpf(f[2]), mpf(f[3]), mpf(f[4]))
            # the last species as the EXACT remainder, matching both the reference and
            # MassFractionState's own requirement
            y = {"H2": y_h2, "O2": y_o2, "H2O": mpf(1) - y_h2 - y_o2}
            rho = p / (ref.mixture_gas_constant(y) * t)
            q = ref.progress_rate(ref.A, ref.N, ref.EA, t, y, rho)
            om = ref.species_sources(q)
            where = f"yH2={f[1]} p={f[3]} T={f[4]}"

            record("rho", rel_eps(f[5], rho), where)
            if q == 0:
                # C_H2 = 0: q and every omega_i must be EXACTLY 0. A relative error is undefined,
                # so this is checked as exact equality -- sharper, not weaker.
                counts["MATRIX_ZERO"] += 1
                for name, field in (("q", f[6]), ("omega_H2", f[7]), ("omega_O2", f[8]),
                                    ("omega_H2O", f[9])):
                    if mpf(field) != 0:
                        failures.append(f"{name} {where}: {field} is not exactly 0")
            else:
                counts["MATRIX"] += 1
                record("q", rel_eps(f[6], q), where)
                record("omega_H2", rel_eps(f[7], om["H2"]), where)
                record("omega_O2", rel_eps(f[8], om["O2"]), where)
                record("omega_H2O", rel_eps(f[9], om["H2O"]), where)
                # sum_i omega_i, judged against the scale P18.4's own diagnostic reports
                total, scale = mpf(f[10]), mpf(f[11])
                record("|sum omega|/scale", (total / scale) / EPS, where)
            rows += 1

        elif f[0] == "MATRIX_ROWS":
            if int(f[1]) != rows:
                failures.append(f"MATRIX_ROWS says {f[1]} but {rows} rows were parsed")

    print("=== P18.5: the probe's doubles against mpmath (dps=60) ===")
    print(f"reference : tools/arrhenius_reference.py, the module G6 names")
    print(f"threshold : {LIMIT} eps relative, G6's own, inherited verbatim")
    print(f"eps       : {float(EPS):.17g}")
    print()
    print(f"k(T) points compared      : {counts['KT']}")
    print(f"matrix rows, non-zero rate: {counts['MATRIX']}")
    print(f"matrix rows, exactly zero : {counts['MATRIX_ZERO']}")
    print()

    # Exactly one non-numeric row is expected: the MATRIX column header the probe prints. Anything
    # else means a real row was not parsed, which would quietly shrink the comparison.
    expected_header = "MATRIX yH2 yO2 p T rho q omega_H2 omega_O2 omega_H2O sumAbs srcAbs"
    if skipped != [expected_header]:
        print(f"FAIL: unexpected unparsed rows: {skipped}")
        return 1

    # THE PARSE MUST NOT BE VACUOUS. A log that matched nothing would otherwise report a clean pass.
    if counts["KT"] == 0 or counts["MATRIX"] == 0:
        print("FAIL: the log produced no comparable rows -- this comparison would be vacuous")
        return 1
    if counts["MATRIX_ZERO"] == 0:
        print("FAIL: no exactly-zero row was found; the zero-concentration case is not covered")
        return 1

    print("worst relative error, in units of eps:")
    for kind in ("k(T)", "x(T)", "rho", "q", "omega_H2", "omega_O2", "omega_H2O",
                 "|sum omega|/scale"):
        if kind in worst:
            value, where = worst[kind]
            verdict = "PASS" if value <= LIMIT else "FAIL"
            print(f"  {kind:<20} {float(value):>8.2f} eps   {verdict}   ({where})")
    print()

    if failures:
        print(f"FAIL: {len(failures)} comparison(s) outside {LIMIT} eps")
        for item in failures:
            print(f"  {item}")
        return 1
    print(f"PASS: every compared quantity is within {LIMIT} eps of the mpmath reference")
    return 0


if __name__ == "__main__":
    sys.exit(main())
