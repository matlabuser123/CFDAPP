#!/usr/bin/env python3
"""GATE_A3 dry-run patches. Each asserts EXACTLY ONE site and refuses to write otherwise.

Written as a file rather than inlined in the harness because a shell heredoc has collapsed `\\n`
into a real newline inside C++ string literals FIVE times in this campaign, once in this very
item's probe. Source text now goes through files only.

  locator_out  remove the locator fix -> reconstructs the PRE-FIX library (M1)
  heat         scale the reactor's heat release by 0.9999 -> C5a's control (M3)
  species      scale the species update by 1.0001 -> C5b's control (M4)
"""
import hashlib
import io
import sys

PATCHES = {
    # M1: the one statement TD-004 added inside recentre().
    "locator_out": (
        "    previousLocated -= static_cast<Real>(shift) * cellSize;\n",
        "",
    ),
    # M3: PremixedFlame.cpp:313. Its own comment states this line shares cp and the formation
    # enthalpies with the energy equation "so the two agree on T_ad by construction" -- scaling it
    # breaks exactly that agreement, which is what makes it a control for C5a and not for anything
    # else. The probe computes T_ad independently, so T_ad itself is unaffected.
    "heat": (
        "cellTemperature[0] += step * heat[0] / (cellDensity[0] * specificHeat);",
        "cellTemperature[0] += 0.9999 * step * heat[0] / (cellDensity[0] * specificHeat);",
    ),
    # M4: PremixedFlame.cpp:309. Over-integrates the species update. Scaling EVERY species by the
    # same factor leaves sum(sources) = 0, so composition still sums to one and C5d is not
    # collaterally corrupted -- the fuel simply over-depletes and goes negative where Y_F -> 0.
    "species": (
        "cellComposition[i][0] += step * sources.perSpecies[i].Su[0] / cellDensity[0];",
        "cellComposition[i][0] += 1.0001 * step * sources.perSpecies[i].Su[0] / cellDensity[0];",
    ),
    # M5: PremixedFlame.cpp:662, the linear-solve acceptance fraction. This is THE control C5b's
    # derivation implies and the one that should have been chosen first: C5b's bound IS
    # `residualAcceptanceFraction * Y_F,u`, so raising the fraction 1000x must raise the admissible
    # excursion 1000x. It therefore tests the DERIVATION, not just the criterion:
    #   |min Y| grows ~1000x to ~1.9e-08  -> C5b is linear-acceptance-limited as derived, and this
    #                                       is its negative control (fails 5e-11 by ~378x)
    #   |min Y| stays ~1e-11              -> THE DERIVATION IS WRONG. min Y is limited by something
    #                                       else, C5b cannot be frozen on this basis, and the bound
    #                                       must be re-derived from whatever actually governs it.
    # No prediction is registered between those two.
    "acceptance": (
        "const Real residualAcceptanceFraction = 1.0e-9;",
        "const Real residualAcceptanceFraction = 1.0e-6;",
    ),
}


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] not in PATCHES:
        sys.stderr.write("usage: a3_patch.py {%s} <file>\n" % "|".join(PATCHES))
        return 2
    mode, path = sys.argv[1], sys.argv[2]
    old, new = PATCHES[mode]

    text = io.open(path, encoding="utf-8", newline="").read()
    needle = old
    if needle not in text and needle.endswith("\n"):
        needle = needle.replace("\n", "\r\n")  # the repo stores bytes verbatim; tolerate either
    count = text.count(needle)
    if count != 1:
        sys.stderr.write(
            "*** ABORT: patch '%s' matched %d sites, expected exactly 1. Nothing written.\n"
            % (mode, count)
        )
        return 1

    replacement = new
    if needle != old and new:
        replacement = new  # single-line patches carry no newline, so nothing to translate
    out = text.replace(needle, replacement)
    if out == text:
        sys.stderr.write("*** ABORT: patch '%s' changed nothing. Nothing written.\n" % mode)
        return 1

    io.open(path, "w", encoding="utf-8", newline="").write(out)
    print(
        "patched '%s': 1 site, source sha256 %s"
        % (mode, hashlib.sha256(out.encode("utf-8")).hexdigest()[:16])
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
