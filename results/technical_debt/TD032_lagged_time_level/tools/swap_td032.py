# TD-032 -- swap PremixedFlame.cpp between its PRE-FIX and POST-FIX forms, with a HASH PROOF.
#
#   python3 swap_td032.py out <repo>    -> reconstruct the PRE-FIX source  (the lagged form)
#   python3 swap_td032.py in  <repo>    -> restore the POST-FIX source     (the fixed form)
#
# WHY A HASH PROOF IS THE WHOLE POINT. CLAUDE.md section 6 requires two libraries before a change
# can be blamed for a failure, and a reconstruction is only admissible if it reproduces the frozen
# sha256 EXACTLY. A previous reverse-apply in this campaign dropped a single inserted blank line and
# produced a plausible-but-wrong baseline; the hash check caught it. So this script refuses to
# write anything whose hash does not match the recorded one, and the caller must treat a refusal as
# a hard stop rather than proceeding with "close enough".
import hashlib
import io
import sys

PRE_FIX_SHA = "dac2992006a51cce6ea9e9f497dc44aceac76026c8d1cc4405882cc38642e5a3"
POST_FIX_SHA = "f9f554bf9e1fe1fd29f6ce4c185e7d579fa5c5708958112bc83127e4a6717b6e"

REL = "src/species/PremixedFlame.cpp"

# ---------------------------------------------------------------- the two forms, byte for byte
PRE_TOP = """  const auto advance = [&](Real currentMassFlux) -> PremixedFlameStatus {
    densityFromState(composition, temperature, density);"""

POST_TOP = """  const auto advance = [&](Real currentMassFlux) -> PremixedFlameStatus {
    // ---- THE OLD TIME LEVEL IS CAPTURED HERE, WHERE THE STEP BEGINS. TD-032.
    //
    // `temperatureOld` and `compositionOld` are the transient term's level-n fields: the energy
    // assembly reads the first through `ThermalTimeState::temperatureOld` and each species assembly
    // reads the second through `SpeciesTimeState::fieldsOld`. Both are consumed at the TOP of a
    // step, so both must hold the state THIS step starts from.
    //
    // They used to be assigned much further down, in the same three-line block as `densityOld`, at
    // a point where `temperature` and `composition` still held their start-of-step values -- so the
    // copy was a no-op on the first step of a march and ONE STEP STALE on every step after it. The
    // transient term then read level k-1 while the operator coefficients came from level k, which
    // connects step k-1 to step k+1: the even and odd steps become two interleaved sub-sequences,
    // coupled only through those coefficients and not through the transient term at all, and two
    // sub-sequences with no transient coupling need not share a fixed point. They did not. The
    // reported field alternated EVERY STEP between the cold preheat zone and a nearly fully burnt
    // state 1484 K away, and the reported front between the anchor and the inlet. Measured at 500
    // and 250 cells; `results/technical_debt/TD032_lagged_time_level/`.
    //
    // Because the first step of every march is seeded `old = current` (at each `runTrial` and
    // before the final march), a one-step check could never see this. Nor could the transient tests
    // in `tests/unit/thermal/` and `tests/unit/transport/`: they hand an assembly an old level and
    // check what it builds, and the assembly was always correct. The defect was entirely in which
    // field this caller handed it.
    //
    // `densityOld` is deliberately NOT moved here. It is a WITHIN-step quantity -- the `dt`
    // controller at the end of this lambda compares the post-chemistry density against it -- so
    // start-of-step is exactly what it needs and it is correct where it is. One assignment site was
    // serving two different requirements, and satisfied the one belonging to the field that never
    // crossed a step boundary.
    temperatureOld = temperature;
    compositionOld = composition;

    densityFromState(composition, temperature, density);"""

PRE_MID = """    temperatureOld = temperature;
    compositionOld = composition;
    densityOld = density;
"""

POST_MID = """    // TD-032: `temperatureOld` and `compositionOld` are now captured at the TOP of this lambda --
    // they cross a step boundary and were one step stale here. `densityOld` stays: it is consumed
    // by the `dt` controller below, inside this same step, where start-of-step is what is wanted.
    densityOld = density;
"""


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("out", "in"):
        sys.exit("usage: swap_td032.py {out|in} <repo-root>")
    direction, repo = sys.argv[1], sys.argv[2].rstrip("/")
    path = repo + "/" + REL

    s = io.open(path, encoding="utf-8", newline="").read()

    if direction == "out":
        pairs = [(POST_TOP, PRE_TOP), (POST_MID, PRE_MID)]
        want, label = PRE_FIX_SHA, "PRE-FIX (lagged)"
    else:
        pairs = [(PRE_TOP, POST_TOP), (PRE_MID, POST_MID)]
        want, label = POST_FIX_SHA, "POST-FIX (fixed)"

    for old, new in pairs:
        n = s.count(old)
        if n != 1:
            sys.exit("*** REFUSED: count=%d for a %d-char anchor. The tree is not in the form this "
                     "direction expects; nothing written." % (n, len(old)))
        s = s.replace(old, new, 1)

    got = hashlib.sha256(s.encode("utf-8")).hexdigest()
    if got != want:
        sys.exit("*** REFUSED: the reconstruction does NOT match the recorded sha256.\n"
                 "    want %s\n    got  %s\n"
                 "    Nothing was written. A plausible-but-wrong baseline is worse than none:\n"
                 "    CLAUDE.md section 6 admits a reconstruction only if its hash matches."
                 % (want, got))

    io.open(path, "w", encoding="utf-8", newline="").write(s)
    print("wrote %s  %s  sha256 %s... PROVED" % (REL, label, got[:16]))


if __name__ == "__main__":
    main()
