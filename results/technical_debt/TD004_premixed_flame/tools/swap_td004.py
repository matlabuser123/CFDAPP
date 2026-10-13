# TD-004 -- swap PremixedFlame.cpp between its PRE-FIX and POST-FIX forms, with a HASH PROOF.
#
#   python3 swap_td004.py out <repo>    -> PRE-FIX: recentre() does NOT translate previousLocated
#   python3 swap_td004.py in  <repo>    -> POST-FIX: it does
#
# The fix is one statement plus its derivation comment, so the reverse-apply is small -- which is
# exactly when a silent near-miss is most likely. This script therefore refuses to write anything
# whose sha256 does not match the recorded value. An earlier reverse-apply in this campaign dropped
# a single inserted blank line and produced a plausible-but-wrong baseline; the hash caught it, and
# CLAUDE.md section 6 admits a reconstruction only if its hash matches.
import hashlib
import io
import sys

PRE_FIX_SHA = "8c85bc9f5f457bf22e1c19143db35a9e28db9ae957a1139f503d88da5e6eae2a"
POST_FIX_SHA = "3c2263cd34f3ae93e7605fa56b4c7823f077b567c5ed9cc8ebd3b95da30204a9"

REL = "src/species/PremixedFlame.cpp"

PRE = """    cumulativeShift += static_cast<Real>(shift) * cellSize;
    ++result.recentrings;"""

POST = """    // ---- TD-004: TRANSLATE THE LOCATOR'S TRACKED POSITION WITH THE FIELD.
    //
    // `locateFront` accepts a crossing only within `admissibleJump` of `previousLocated`. This
    // operator moves the whole field and used to leave that tracked position behind, so the FIRST
    // recentring of a march put the front out from under the locator and every later crossing then
    // looked like an inadmissible jump. Measured consequence: the locator refused the TRUE front
    // once per step for the remaining 16 000+ steps, `recentre` was never invoked again, and the
    // front drifted 51 cells off the anchor at relaxT 10 and 86 at relaxT 30. Evidence:
    // `results/technical_debt/TD004_premixed_flame/audit_A2.md`.
    //
    // THE SIGN IS DERIVED, not chosen. `shiftField` sets `shifted[c] = field[c + shift]`, so a
    // feature at index `i` moves to index `i - shift` and its position DECREASES by
    // `shift * cellSize`. The tracked position must move the same way, which is also why
    // `cumulativeShift` ADDS the same quantity below: that one converts a shifted position back to
    // an absolute one for the drift history, so the two have opposite signs by construction.
    //
    // WHY NOT RESET `haveLocated` INSTEAD, which is what the RE-SEED path at the final march does:
    // that path's first-call branch accepts ANY crossing, which disarms the jump guard for a step.
    // Re-seeding can afford that because it moves the front discontinuously and no prior position
    // is meaningful afterwards. A translation is different -- the displacement is known exactly --
    // so the guard stays armed and the tracked position is corrected instead.
    //
    // Checked against the measurement: previousLocated 3.559962 mm, shift 3, cellSize 2e-5 m gives
    // 3.499962 mm, and the front immediately after that shift sits at ~3.4999 mm. The distance the
    // locator then measures collapses to ~0 instead of 3.287 cells, and tracking continues.
    previousLocated -= static_cast<Real>(shift) * cellSize;
    cumulativeShift += static_cast<Real>(shift) * cellSize;
    ++result.recentrings;"""


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("out", "in"):
        sys.exit("usage: swap_td004.py {out|in} <repo-root>")
    direction, repo = sys.argv[1], sys.argv[2].rstrip("/")
    path = repo + "/" + REL

    s = io.open(path, encoding="utf-8", newline="").read()
    if direction == "out":
        old, new, want, label = POST, PRE, PRE_FIX_SHA, "PRE-FIX (locator not translated)"
    else:
        old, new, want, label = PRE, POST, POST_FIX_SHA, "POST-FIX (locator translated)"

    n = s.count(old)
    if n != 1:
        sys.exit("*** REFUSED: count=%d for the anchor. The tree is not in the form this direction "
                 "expects; nothing written." % n)
    s = s.replace(old, new, 1)

    got = hashlib.sha256(s.encode("utf-8")).hexdigest()
    if got != want:
        sys.exit("*** REFUSED: the reconstruction does NOT match the recorded sha256.\n"
                 "    want %s\n    got  %s\n"
                 "    Nothing was written." % (want, got))

    io.open(path, "w", encoding="utf-8", newline="").write(s)
    print("wrote %s  %s  sha256 %s... PROVED" % (REL, label, got[:16]))


if __name__ == "__main__":
    main()
