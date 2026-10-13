#!/usr/bin/env bash
# P17.8 -- the STRUCTURAL criteria of the frozen gate: A1-A6, Q2-Q4, R3, R5, U4, M1, M3, P4, S3.
#
# Every count below is paired with a SANITY control that must be NON-ZERO. A probe that greps for a
# renamed symbol, or reads a file that does not exist, prints exactly what success looks like; the
# sanity line is what distinguishes "0 because the property holds" from "0 because the probe is
# blind". This is the fail-open shape recorded in P17.6 (B6) and P17.7 (audit section 6).
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
HDR=include/cfd/compressible/DensityBasedSolver.hpp
SRC=src/compressible/DensityBasedSolver.cpp

fail=0
note() { printf '  %-58s %s\n' "$1" "$2"; }
require() {
  if [ "$2" = "$3" ]; then note "$1" "$2   OK"
  else note "$1" "$2   *** EXPECTED $3 ***"; fail=$((fail+1)); fi
}
for f in "$HDR" "$SRC"; do
  if [ ! -f "$f" ]; then
    echo "PROBE INPUT MISSING: $f -- every count below would be vacuously 0"
    exit 2
  fi
done

echo "P17.8 STRUCTURAL CRITERIA"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse HEAD)"
echo "probe inputs: $HDR ($(grep -c . "$HDR") lines), $SRC ($(grep -c . "$SRC") lines)"
echo ""

echo "### Group A -- SIMPLE preservation ###"
untouched=0
touched=0
check() {
  local got
  got=$(sha256sum "$2" 2>/dev/null | awk '{print $1}')
  if [ -z "$got" ]; then
    echo "  *** MISSING FILE: $2"
    touched=$((touched+1))
    return
  fi
  if [ "$got" = "$1" ]; then
    untouched=$((untouched+1))
  else
    touched=$((touched+1))
    echo "  *** TOUCHED: $2"
    echo "        got      $got"
    echo "        expected $1"
  fi
}

echo "-- A1/A2: the dispatch and both SIMPLE paths, against logs/02_freeze.log"
check 3f8214b4412f1877bbd2c98446ac08b00ea6c2662c98aea9e7b2970b7d714a4a src/app/ProjectRunner.cpp
check b0109552ffd6b8eedbba6a5ab274b3771fd89373a27bf21bf0619cc4e93f0ff7 include/cfd/app/ProjectRunner.hpp
check 34f4f99577a9e4f6c35614291f3ea4ab246809af34fb08c81ac5b3b8f7d6ecd1 src/pressure_velocity/SIMPLE.cpp
check 325a7f380be58144794538cd64d0fd4f1324a46b1bc9fa2727d5fa899e2eb9da include/cfd/pressure_velocity/SIMPLE.hpp
check f95f760bbe2abb284ec33161209bf33f25ec9a0fb4a02e170a2232bd17d6d8ed src/compressible/CompressibleSIMPLE.cpp
check 102879c99bd54e89591bab34cdaa2ebd9da80fd8a6b1cddd927f195bfbb79898 include/cfd/compressible/CompressibleSIMPLE.hpp

echo "-- A5: the EXISTING CFL and transient facilities -- extended, never modified"
check 25f7e5a46bb6f2da447f29b5d23ac9fd1a8f5988627d755f5715e26913f73001 include/cfd/solver/CFL.hpp
echo "    02_freeze.log recorded CFL.hpp only. The other three A5 files are TRACKED, so git is"
echo "    authoritative for them and is what is checked:"
for f in src/solver/CFL.cpp include/cfd/solver/TransientSolver.hpp src/solver/TransientSolver.cpp; do
  d=$(git diff --stat HEAD -- "$f" | wc -l)
  require "git-clean vs HEAD: $f" "$d" "0"
  note "  sha256 now" "$(sha256sum "$f" | awk '{print $1}')"
done

echo "-- A6: every P17.1-P17.7 production source UNTOUCHED (P17.7's own list, verbatim)"
check 94d5b7c20f530fd9e84ed5f114b58074e26466f410b34b247a883025d20b5896 include/cfd/core/Vector3.hpp
check bdd096490437ee602de31695184035e2ce28f0ab4d2da6187e45c709e919324e include/cfd/compressible/IdealGasEOS.hpp
check daa27c5fc7e296482fd29fd93a0b4b6185fc7f3d39bc72855aa6240aad3bfe24 src/compressible/IdealGasEOS.cpp
check 1dd39383f5d87eb77a5b69252737db0cd0640121ea6e19eb5ec3916377008335 include/cfd/compressible/ThermodynamicProperties.hpp
check 27d74d9e535f8e716421ce24e7f955f32dc8200c5ae5a3ddfd1c9cbdb4da3a88 src/compressible/ThermodynamicProperties.cpp
check 16cb9b91ce6fd9de1011b064b3798c5a80304f3e9520722f1a141c9c7a5aa661 include/cfd/compressible/EquationOfState.hpp
check a15726f9ffce7f14871f18446ad70129df4c9b34de5d3e8fdaf855abbabe265d include/cfd/compressible/TotalEnergy.hpp
check 23bde46f7a94d7e24ed262ef3ae5f4cc5677bb9194e01399014b12d929df16c5 src/compressible/TotalEnergy.cpp
check 02067df44148ef0ed4171780344779cbf25e7743909c0e0142d0e145517f5e03 include/cfd/compressible/TotalEnergyEquation.hpp
check 2df1c9f54d5186fb5f22cf1f74fbb3c3aaaaddbbebef929df1853441b2c1956e src/compressible/TotalEnergyEquation.cpp
check 2864c6ebb8b85b35ffd32c88544bb427fa3d87cd573569c60b6b53a808d95ca7 src/compressible/CompressibleMassFlux.cpp
check 5396726d935905d450df2bdb01d215ed29136f23d2f0f78f6dae4563b0b1f267 src/compressible/CompressibleContinuity.cpp
check 0bdff82ec3e717bc8fc8aac01f5bf06914153039673bcf43cfcfe8902532007d src/compressible/CompressibleRelaxedMomentum.cpp
check fc00192ddb9d024fa1c61fcf6d7911fb7b862ec811ddc503b10e18370bc0c5e3 src/compressible/DilatationalStress.cpp
check 593165a1a2fff9ccbad8b3675ccbdff093bcf22c82b5daad90462fe228949acc tests/integration/compressible/test_low_mach_regression.cpp
check 01838fd3a113420641ef878550dde080adb9d39a0326b046a7ca4ea4fee895c5 tests/integration/compressible/test_low_mach_family.cpp
check 9c3ad6502e65543422e95c21e99354380c2cffeedeafd2d8b4b9fa3c83793796 tests/integration/mms/test_mms_compressible.cpp
check c33143bfc886b0695e8a11b852f0035575f8f8389ee24e4d60a47888e863d674 tests/integration/poiseuille/test_poiseuille_validation.cpp
check 8a6061c30da34bff819beca7c563126e636b505dca4c074c85ee6503ccd949a0 tests/integration/cavity/test_cavity_ghia.cpp
check 16889dd97925fa33de8e25f91789ddafc3fb90c599cb474928c7a31cc5ccb39d src/compressible/HllcFlux.cpp

echo "-- A6 EXCEPTION, declared: HllcFlux.hpp, corrected by AMENDMENT_star_energy_sign.md"
HLLC_NOW=$(sha256sum include/cfd/compressible/HllcFlux.hpp | awk '{print $1}')
note "include/cfd/compressible/HllcFlux.hpp now" "$HLLC_NOW"
note "  at the P17.7 close (hllc/logs/20_integrity.log)" "98885f49b987d1ec09b93492aa12231277296b1cb6f34bb4a1e92cf08b5c0cdd"
if [ "$HLLC_NOW" = "98885f49b987d1ec09b93492aa12231277296b1cb6f34bb4a1e92cf08b5c0cdd" ]; then
  note "  CHANGED?" "NO -- then the sign correction was NOT applied ***"
  fail=$((fail+1))
else
  note "  CHANGED?" "YES -- the star-energy sign correction, amended and re-verified"
fi
echo "  UNTOUCHED: $untouched    TOUCHED: $touched"
echo "  (HllcFlux.hpp is counted NEITHER way -- it is the declared exception, on its own line above.)"
require "A1/A2/A5/A6 TOUCHED count" "$touched" "0"
echo ""

echo "-- A4: references to the density-based solver from any SIMPLE / pressure-based path"
A4=$(grep -rlE 'DensityBasedSolver|density_based' src/pressure_velocity/ include/cfd/pressure_velocity/ \
      src/compressible/CompressibleSIMPLE.cpp include/cfd/compressible/CompressibleSIMPLE.hpp \
      src/compressible/CompressibleRelaxedMomentum.cpp src/app/ProjectRunner.cpp 2>/dev/null | wc -l)
require "files under a SIMPLE path naming the solver" "$A4" "0"
A4S=$(grep -rlE 'SIMPLE' src/pressure_velocity/ 2>/dev/null | wc -l)
note "SANITY: those same paths DO match a control pattern" "$A4S files match SIMPLE (must be > 0)"
if [ "$A4S" -le 0 ]; then fail=$((fail+1)); fi
echo ""

echo "### Group Q -- no duplicated upstream physics ###"
# The FIRST version of this pattern looked for 'gamma - 1' and 'specificGasConstant'. Its sanity
# control returned 0 on HllcFlux.hpp, which exposed the probe as blind: the real code writes
# (gamma + 1.0)/(2.0*gamma) and gasConstant_, neither of which that pattern matched. The 0 on the
# solver was therefore meaningless. Corrected to match gamma in ANY arithmetic context plus the real
# gas-constant identifier, and re-checked against THREE upstream physics files, each of which must
# be non-zero for the solver's 0 to mean anything.
Q2PAT='gamma *[-+*/] *[0-9(a-z]|[0-9)a-z] *[-+*/] *gamma|gasConstant|sqrtR\([^)]*gamma|pressure *= *[^;=]*density *\*'
Q2=$(grep -cE "$Q2PAT" "$HDR" "$SRC" | awk -F: '{s+=$2} END{print s+0}')
require "Q2: EOS / sound-speed / gamma-arithmetic / p=rho R T" "$Q2" "0"
for ctl in include/cfd/compressible/HllcFlux.hpp src/compressible/IdealGasEOS.cpp src/compressible/TotalEnergy.cpp; do
  c=$(grep -cE "$Q2PAT" "$ctl")
  note "SANITY: the same pattern on $(basename "$ctl")" "$c hits (must be > 0, else the 0 is blind)"
  if [ "$c" -le 0 ]; then fail=$((fail+1)); fi
done
Q3A=$(grep -cE 'makePrimitiveStateFromConserved' "$SRC")
Q3B=$(grep -cE 'makePrimitiveState\(' "$SRC")
Q3C=$(grep -cE 'toConserved\(' "$SRC")
note "Q3: makePrimitiveStateFromConserved call sites (>=1)" "$Q3A"
note "Q3: makePrimitiveState call sites (>=1)" "$Q3B"
note "Q3: toConserved call sites (>=1)" "$Q3C"
if [ "$Q3A" -lt 1 ] || [ "$Q3B" -lt 1 ] || [ "$Q3C" -lt 1 ]; then fail=$((fail+1)); fi
Q4=$(grep -cE 'std::vector<PrimitiveState> +[a-zA-Z_]+_;|PrimitiveState +[a-zA-Z_]+_;' "$HDR")
require "Q4: primitives STORED as a member beside Q" "$Q4" "0"
Q4S=$(grep -cE '[A-Za-z]+ +[a-zA-Z_]+_;' "$HDR")
note "SANITY: the member-declaration pattern matches in the header" "$Q4S members (must be > 0)"
if [ "$Q4S" -le 0 ]; then fail=$((fail+1)); fi
echo ""

echo "### Group R -- the residual ###"
R3=$(grep -cE 'hllcFlux(Checked)?\(' "$SRC")
require "R3: hllcFlux call sites in the solver (exactly 1)" "$R3" "1"
R5A=$(grep -cE 'r\.density\[owner\] \+=' "$SRC")
R5B=$(grep -cE 'r\.density\[neighbour\] -=' "$SRC")
require "R5: +F owner assignment" "$R5A" "1"
require "R5: -F neighbour assignment" "$R5B" "1"
R5REF=$(grep -cE '\+=|-=' src/compressible/CompressibleContinuity.cpp)
note "R5 reference: CompressibleContinuity.cpp +=/-= lines" "$R5REF (repository convention, > 0)"
if [ "$R5REF" -le 0 ]; then fail=$((fail+1)); fi
echo ""

echo "### Group U / M / P / S ###"
U4=$(grep -cE 'settings_\.mode *=[^=]|\.mode *= *TimeSteppingMode' "$SRC")
require "U4: mode assignments outside construction" "$U4" "0"
U4S=$(grep -cE 'settings_\.mode' "$SRC")
note "SANITY: settings_.mode is READ in the solver" "$U4S reads (must be > 0)"
if [ "$U4S" -le 0 ]; then fail=$((fail+1)); fi
M1=$(sed -n '/^struct ResidualNorms/,/^};/p' "$HDR" | grep -cE '^  Real ')
require "M1: five per-equation norms in ResidualNorms" "$M1" "5"
M3=$(grep -cE 'referenceCaptured' "$SRC")
note "M3: the capture-once guard appears" "$M3 times (must be > 0)"
if [ "$M3" -le 0 ]; then fail=$((fail+1)); fi
M3B=$(grep -cE 'reference = absolute' "$SRC")
require "M3: assignments to the reference scale (exactly 1)" "$M3B" "1"
P4PAT='std::clamp|clampDensity|floorPressure|kMinDensity|kDensityFloor|kPressureFloor'
P4=$(grep -cE "$P4PAT" "$HDR" "$SRC" | awk -F: '{s+=$2} END{print s+0}')
require "P4: clamp / floor idioms on a state quantity" "$P4" "0"
P4S=$(printf 'rho = std::clamp(rho, kMinDensity, big);\n' | grep -cE "$P4PAT")
note "SANITY: the clamp pattern matches a synthetic clamp line" "$P4S (must be 1, else the 0 is blind)"
if [ "$P4S" -lt 1 ]; then fail=$((fail+1)); fi
S3PAT='viscous|viscosity|conduction|conductivity|heatFlux|stressTensor'
S3=$(grep -ciE "$S3PAT" "$HDR" "$SRC" | awk -F: '{s+=$2} END{print s+0}')
note "S3: viscous / conduction mentions, comments included" "$S3"
S3C=$(grep -inE "$S3PAT" "$HDR" "$SRC" | grep -cvE ':[0-9]+: *(//|\*)')
require "S3: viscous / conduction mentions in CODE, not comments" "$S3C" "0"
echo ""
echo "STRUCTURAL FAILURES: $fail   (must be 0)"
if [ "$fail" -eq 0 ]; then exit 0; else exit 1; fi
