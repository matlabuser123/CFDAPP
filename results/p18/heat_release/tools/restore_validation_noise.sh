#!/usr/bin/env bash
# P18.6 -- classify and restore results/validation after the P18.6 regression.
#
# WHY THESE FILES CHANGE AT ALL. Every full ctest run regenerates tracked reports under
# results/validation. CLAUDE.md section 7 requires the changes to be CLASSIFIED and the noise
# restored -- not committed, and not blindly reverted either.
#
# WHAT THE CHANGE IS, established by experiment in P17.11 and recorded as KNOWN_LIMITATIONS L36:
# the tracked files are RELEASE-generated. Running the same MMS test from each configuration's
# binary in turn, restoring between runs, gave
#     release_cuda  -> changed keys ALL "seconds"               timing only
#     cpu_only      -> changed keys ALL "seconds"               timing only
#     debug_gui     -> l1/l2/linf/observed_order/... changed    158 differing lines
# because an iterative solve converges to a slightly different point under different optimisation.
# So the Debug+GUI pass overwrites Release numbers with Debug numbers, and restoring returns the
# repository to its correct state.
#
# HOW THIS VERSION DIFFERS FROM P18.4's. P18.4 asserted the attribution in a comment and counted
# one key ("wall_shear") before reverting. This enumerates EVERY changed key and requires each one
# to be in an explicit allow-list, and separately requires a ZERO count for each physics field
# that must not move. It FAILS CLOSED and refuses to restore if anything else changed -- because
# the whole risk here is a real regression hiding inside an expected-looking pile of noise, and a
# narrow probe is how that gets missed.
set -uo pipefail
cd "$(dirname "$0")/../../../.." || exit 1

if pgrep -f p18_6_regress > /dev/null 2>&1; then
  echo "FAIL: a regression configuration is still running -- refusing to restore"
  exit 2
fi

BEFORE=$(git status --porcelain -- results/validation | wc -l)
echo "=== P18.6: results/validation noise classification ==="
echo "date  : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "files modified by the P18.6 regression : $BEFORE"
if [ "$BEFORE" -eq 0 ]; then
  echo "nothing to classify or restore."
  exit 0
fi
echo ""

echo "--- by directory ---"
git status --porcelain -- results/validation | awk '{print $2}' | xargs -n1 dirname 2>/dev/null \
  | sort | uniq -c | sed 's/^/  /'
echo ""

# Every changed line's leading identifier, de-quoted. Pure-numeric and grid-label lines (values in
# a markdown table) are dropped -- they are the VALUES of the keys below, not keys themselves.
git diff -U0 -- results/validation | grep -E '^[+-][^+-]' \
  | sed -E 's/^[+-]//; s/^[ \t|]*//; s/^\**//; s/[ \t]*[|:=].*//; s/[ \t].*//' \
  | sed 's/\**$//; s/^"//; s/"$//' \
  | grep -vE '^$' \
  | grep -vE '^[0-9]+(\.[0-9]+)?,?$' \
  | grep -vE '^[0-9]+x[0-9]+$' \
  | grep -vE '^(coarse|medium|fine)$' \
  | grep -vE '/' \
  | sort -u > /tmp/p18_6_keys.txt

# TIMING -- expected and meaningless.
# ITERATIVE -- the quantities L36 describes: norms, observed orders, Richardson/GCI products and
# final residuals, all of which depend on where an iterative solve stopped.
ALLOW='^(seconds|runtime_seconds)$|^(l1|l2|linf|velocity_l2_error)$|^(observed_order|finest_observed_order|asymptotic_ratio|convergence_ratio|order_ratio)$|^(extrapolated_error|error_extrapolated|richardson_extrapolated|richardson_extrapolated_32)$|^(error_fine|error_medium|relative_error_fine|relative_error_medium)$|^(approximate_relative_error_21|approximate_relative_error_32|extrapolated_relative_error_21|extrapolated_relative_error_32)$|^(epsilon21|epsilon32|uncertainty_21|uncertainty_32|gci_fine_medium|gci_medium_coarse)$|^(final_continuity_residual|final_pressure_residual|final_u_residual|final_v_residual)$|^(pressure_gradient|radial_pressure_rise_ratio)$|^(detail|diagnostic)$'

echo "--- every distinct changed key, against the allow-list ---"
echo "  distinct keys : $(wc -l < /tmp/p18_6_keys.txt)"
UNEXPECTED=$(grep -vE "$ALLOW" /tmp/p18_6_keys.txt || true)
if [ -n "$UNEXPECTED" ]; then
  echo "  *** KEYS OUTSIDE THE ALLOW-LIST -- REFUSING TO RESTORE ***"
  echo "$UNEXPECTED" | sed 's/^/      /'
  echo ""
  echo "  These are NOT classified as noise. Investigate before restoring; a real regression"
  echo "  hiding in expected-looking noise is exactly what this interlock exists for."
  exit 1
fi
echo "  all keys are timing or iteratively-converged quantities (L36)  ok"
echo ""

echo "--- physics fields that MUST NOT change (each must be 0) ---"
bad=0
for k in wall_shear u_tau y_plus nusselt reattachment cf_ pressure_coefficient mass_flow \
         max_divergence mass_imbalance; do
  c=$(git diff -U0 -- results/validation | grep -E '^[+-][^+-]' | grep -c "$k" || true)
  printf '  %-24s %-4s %s\n' "$k" "$c" "$([ "$c" = "0" ] && echo ok || echo '*** CHANGED ***')"
  [ "$c" = "0" ] || bad=1
done
if [ "$bad" -ne 0 ]; then
  echo ""
  echo "  *** A PHYSICS FIELD MOVED -- REFUSING TO RESTORE ***"
  exit 1
fi
echo ""

echo "--- POSITIVE CONTROL: the classification actually read a diff ---"
LINES=$(git diff -U0 -- results/validation | grep -cE '^[+-][^+-]' || true)
echo "  changed lines inspected : $LINES"
if [ "$LINES" -eq 0 ] || [ "$(wc -l < /tmp/p18_6_keys.txt)" -eq 0 ]; then
  echo "  *** 0 -- the classification is blind; a clean verdict here would be meaningless ***"
  exit 1
fi
echo "  ok (a zero here would make every check above vacuous)"
echo ""

echo "--- magnitude: measured KEY BY KEY, not by pairing diff lines ---"
# THE FIRST VERSION OF THIS BLOCK WAS WRONG AND REPORTED AN ALARMING NUMBER.
# It grepped the l2 lines out of `git diff -U0` and paired the values with `paste - -`, which is
# only correct when the diff alternates -/+. On a GROUPED hunk it pairs grid 1 with grid 2 and
# reports the refinement ratio as if it were drift. On a synthetic diff whose true answer was
# 1.0e-09 it reported 3.0e+00; on this run it reported 4.632e-02 against L36's recorded 4.1e-07,
# a contradiction that was entirely the instrument's.
# See logs/13_noise_measurement_CORRECTED.log.
if ! python3 "$(dirname "$0")/compare_validation_json.py" | sed 's/^/  /'; then
  echo "  *** the magnitude measurement failed -- REFUSING TO RESTORE ***"
  exit 1
fi
echo ""

git checkout -- results/validation
AFTER=$(git status --porcelain -- results/validation | wc -l)
echo "restored.  modified before: $BEFORE   after: $AFTER"
if [ "$AFTER" -ne 0 ]; then
  echo "*** RESTORE INCOMPLETE ***"
  exit 1
fi
echo "results/validation is back to its tracked, Release-generated state."
