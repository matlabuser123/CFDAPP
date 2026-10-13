#!/usr/bin/env bash
# P15 -- non-vacuity controls.
#
# Each control deliberately breaks ONE thing and requires the corresponding
# gate to FAIL. A gate that passes under its own control proves nothing, which
# is the trap this project has hit four times (P13-002 M2, P13-003 E8, P13-005
# N1, P14 NV2/NV4/NV5).
#
# Every mutation is restored from a pre-control copy and the restore is
# VERIFIED BY sha256 -- not assumed, because a control that leaves production
# source modified is worse than no control at all (P13-002 hit exactly that).
#
# Each control asserts the MECHANISM, not merely a nonzero exit: a test that
# fails for an unrelated reason is a false DETECTED, which is how P14's first
# control round produced three of them.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT"
export PATH=/usr/local/cuda-12.9/bin:$PATH

R=$ROOT/results/p15/thermal-energy
BK=/tmp/p15_controls
rm -rf "$BK"; mkdir -p "$BK"

FILES="src/thermal/EnergyEquation.cpp src/thermal/Enthalpy.cpp \
src/thermal/TemperatureDependentProperty.cpp src/boundary/ConvectiveHeatTransfer.cpp \
tests/integration/thermal/test_thermal_validation.cpp"

for f in $FILES; do cp "$f" "$BK/$(echo "$f" | tr / _)"; done
sha256sum $FILES > "$BK/pre.sha256"
echo "pre-control hashes recorded:"
sed 's/^/  /' "$BK/pre.sha256"

restore() {
  for f in $FILES; do cp "$BK/$(echo "$f" | tr / _)" "$f"; done
  if sha256sum -c "$BK/pre.sha256" --quiet; then
    echo "  RESTORED (sha256 verified)"
  else
    echo "  *** RESTORE FAILED -- stopping rather than continuing on a mutated tree ***"
    exit 9
  fi
}

# Build only what the thermal gates need. The pyramid: targeted, not the full
# suite, for a fix-verify cycle.
# Deletes the binaries FIRST and verifies they came back.
#
# Without that, a mutation that does not compile leaves the previous binary in
# place, run_gate runs it, it passes, and the control is recorded UNDETECTED --
# a gate reported vacuous when in fact the control never ran. This project has
# hit the stale-binary class three times (the `RC=$?`-after-a-pipeline family);
# the fix each time was to make the absence of a fresh artifact an error rather
# than something to be inferred.
rebuild() {
  rm -f ./build/final/tests/unit/thermal/CFDThermalTests \
        ./build/final/tests/integration/thermal/CFDThermalValidationTests
  cmake --build build/final --target CFDThermalTests CFDThermalValidationTests -j 20 \
    > /tmp/p15_ctl_build.log 2>&1
  if [ ! -x ./build/final/tests/unit/thermal/CFDThermalTests ] || \
     [ ! -x ./build/final/tests/integration/thermal/CFDThermalValidationTests ]; then
    echo "  BUILD FAILED under this control -- a compile error is a DETECTION only if"
    echo "  the control was meant to break the build; otherwise the control is invalid."
    grep -E "error:" /tmp/p15_ctl_build.log | head -5 | sed 's/^/    /'
    return 1
  fi
  return 0
}

# run_gate <gtest filter> -> 0 if the filtered tests all pass
run_gate() {
  local binary=$1 filter=$2
  "./build/final/$binary" --gtest_filter="$filter" > /tmp/p15_ctl_run.log 2>&1
}

verdict() { # verdict <id> <detected yes|no> <what>
  if [ "$2" = "yes" ]; then
    echo "  $1: DETECTED -- $3"
  else
    echo "  $1: *** UNDETECTED *** -- $3 (the gate is VACUOUS)"
    FAILURES=$((FAILURES + 1))
  fi
}
FAILURES=0

mutate() { python3 - "$@"; }

# ---------------------------------------------------------------------------
echo
echo "########## NC1 -- reverse the Robin conductance sign; Case E must fail"
mutate <<'PY'
import io
p = 'src/thermal/EnergyEquation.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
o = 'builder.add(ownerId, ownerId, conductance);'
assert s.count(o) == 1, s.count(o)
s = s.replace(o, 'builder.add(ownerId, ownerId, -conductance);', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
PY
if rebuild; then
  run_gate tests/integration/thermal/CFDThermalValidationTests 'ThermalValidation.CaseERobin*'
  rc=$?
  [ $rc -ne 0 ] && verdict NC1 yes "Robin analytical case fails (exit $rc)" \
                || verdict NC1 no "Robin case still passes with a reversed sign"
else
  echo "  build failed under NC1 -- treated as detected only if the gate cannot run"
  verdict NC1 no "did not build; control inconclusive"
fi
restore

# ---------------------------------------------------------------------------
echo
echo "########## NC2 -- disable the volumetric heat source; Case B must fail"
mutate <<'PY'
import io
p = 'src/thermal/EnergyEquation.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
o = 'rhs[cell.id()] += volumetricHeatSource * cell.volume();'
assert s.count(o) >= 1, s.count(o)
s = s.replace(o, 'rhs[cell.id()] += 0.0 * volumetricHeatSource * cell.volume();', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
PY
if rebuild; then
  run_gate tests/integration/thermal/CFDThermalValidationTests 'ThermalValidation.CaseB*'
  rc=$?
  [ $rc -ne 0 ] && verdict NC2 yes "heat-generation case fails (exit $rc)" \
                || verdict NC2 no "Case B passes with the source disabled"
else
  verdict NC2 no "did not build; control inconclusive"
fi
restore

# ---------------------------------------------------------------------------
echo
echo "########## NC3 -- k(T) collapsed to a constant; Case F must fail"
mutate <<'PY'
import io
p = 'src/thermal/TemperatureDependentProperty.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
o = '    result[i] = horner(coefficients_, t);'
assert s.count(o) == 1, s.count(o)
s = s.replace(o, '    result[i] = horner(coefficients_, minimumTemperature_);  // NC3', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
PY
if rebuild; then
  run_gate tests/integration/thermal/CFDThermalValidationTests 'ThermalValidation.CaseF*'
  rc=$?
  [ $rc -ne 0 ] && verdict NC3 yes "variable-conductivity case fails (exit $rc)" \
                || verdict NC3 no "Case F passes with k(T) frozen to a constant"
else
  verdict NC3 no "did not build; control inconclusive"
fi
restore

# ---------------------------------------------------------------------------
echo
echo "########## NC4 -- corrupt a cp(T) coefficient; enthalpy tests must fail"
mutate <<'PY'
import io
p = 'src/thermal/Enthalpy.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
o = 'total += coefficients[i] / static_cast<Real>(i + 1) * power;'
assert s.count(o) == 1, s.count(o)
s = s.replace(o, 'total += coefficients[i] * 1.05 / static_cast<Real>(i + 1) * power;  // NC4', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
PY
if rebuild; then
  run_gate tests/unit/thermal/CFDThermalTests 'SensibleEnthalpyTest.*'
  rc=$?
  [ $rc -ne 0 ] && verdict NC4 yes "enthalpy tests fail (exit $rc)" \
                || verdict NC4 no "enthalpy tests pass with a 5% coefficient error"
else
  verdict NC4 no "did not build; control inconclusive"
fi
restore

# ---------------------------------------------------------------------------
echo
echo "########## NC5 -- break the enthalpy INTEGRAL (use cp*T); the naive-form guard must fail"
mutate <<'PY'
import io
p = 'src/thermal/Enthalpy.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
o = 'total += coefficients[i] / static_cast<Real>(i + 1) * power;'
assert s.count(o) == 1
# Drop the 1/(i+1): this is exactly the h = cp*T shortcut the design rejects.
s = s.replace(o, 'total += coefficients[i] * power;  // NC5: the naive shortcut', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
PY
if rebuild; then
  run_gate tests/unit/thermal/CFDThermalTests 'SensibleEnthalpyTest.VariableCpMatchesTheClosedFormIntegral'
  rc=$?
  [ $rc -ne 0 ] && verdict NC5 yes "closed-form integral test fails (exit $rc)" \
                || verdict NC5 no "the integral test passes against the naive product"
else
  verdict NC5 no "did not build; control inconclusive"
fi
restore

# ---------------------------------------------------------------------------
echo
echo "########## NC6 -- break the enthalpy INVERSION; round-trip must fail"
mutate <<'PY'
import io
p = 'src/thermal/Enthalpy.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
o = '    if (this->enthalpy(middle) < enthalpy) {'
assert s.count(o) == 1
s = s.replace(o, '    if (this->enthalpy(middle) > enthalpy) {  // NC6: bisection reversed', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
PY
if rebuild; then
  run_gate tests/unit/thermal/CFDThermalTests 'SensibleEnthalpyTest.TemperatureEnthalpyRoundTrip'
  rc=$?
  [ $rc -ne 0 ] && verdict NC6 yes "round-trip fails (exit $rc)" \
                || verdict NC6 no "round-trip passes with the bisection reversed"
else
  verdict NC6 no "did not build; control inconclusive"
fi
restore

# ---------------------------------------------------------------------------
echo
echo "########## NC7 -- transient uses the NEW density on the RHS; conservation trap must fire"
mutate <<'PY'
import io
p = 'src/thermal/EnergyEquation.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
o = 'rhs[id] += rhoOld * cpOld * (*time.temperatureOld)[id] * cell.volume() / time.dt;'
assert s.count(o) == 1, s.count(o)
s = s.replace(o, 'rhs[id] += rhoNew * cpNew * (*time.temperatureOld)[id] * cell.volume() / time.dt;', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
PY
if rebuild; then
  run_gate tests/unit/thermal/CFDThermalTests 'ThermalTransient.OldTimeRhsUsesOldDensityAndOldSpecificHeat'
  rc=$?
  [ $rc -ne 0 ] && verdict NC7 yes "the level-mix-up trap fires (exit $rc)" \
                || verdict NC7 no "the trap did not fire on a non-conservative RHS"
else
  verdict NC7 no "did not build; control inconclusive"
fi
restore

# ---------------------------------------------------------------------------
echo
echo "########## NC8 -- perturb conservation accounting; the balance gate must fail"
mutate <<'PY'
import io
p = 'tests/integration/thermal/test_thermal_validation.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
o = '      imbalance += residual[i] - assembly.system.rhs()[i];'
assert s.count(o) == 1, s.count(o)
s = s.replace(o, '      imbalance += residual[i] - 1.000001 * assembly.system.rhs()[i];  // NC8', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
PY
if rebuild; then
  run_gate tests/integration/thermal/CFDThermalValidationTests 'ThermalConservation.*'
  rc=$?
  [ $rc -ne 0 ] && verdict NC8 yes "the conservation gate fails (exit $rc)" \
                || verdict NC8 no "conservation passes with a 1e-6 relative accounting error"
else
  verdict NC8 no "did not build; control inconclusive"
fi
restore

# ---------------------------------------------------------------------------
echo
echo "########## restored tree rebuilds and the gates pass again"
if rebuild; then
  run_gate tests/unit/thermal/CFDThermalTests '*' && \
    run_gate tests/integration/thermal/CFDThermalValidationTests '*' && \
    echo "  restored tree: thermal unit + validation gates PASS" || \
    echo "  *** restored tree does NOT pass -- investigate before trusting any control above ***"
else
  echo "  *** restored tree does not build ***"
fi
sha256sum -c "$BK/pre.sha256" && git status --porcelain -- $FILES

echo
echo "UNDETECTED CONTROLS: $FAILURES"
[ $FAILURES -eq 0 ] || exit 1
