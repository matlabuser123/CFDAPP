#!/usr/bin/env bash
# P18.1 -- clang-format, CI settings (CLAUDE.md section 7).
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1

FMT=$(command -v clang-format-18 || command -v clang-format)
echo "formatter: $FMT ($("$FMT" --version))"
echo ""
echo "=== the P18.1 files ==="
"$FMT" --dry-run --Werror \
  include/cfd/species/SpeciesRegistry.hpp \
  include/cfd/species/MassFractionState.hpp \
  src/species/SpeciesRegistry.cpp \
  src/species/MassFractionState.cpp \
  tests/unit/species/test_mass_fraction_state.cpp
echo "  P18.1 files exit: $?"
echo ""
echo "=== the whole tree, as CI runs it ==="
find include src apps tests -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 \
  | xargs -0 "$FMT" --dry-run --Werror 2> /tmp/p18_1_fmt.log
RC=$?
echo "  tree exit: $RC"
if [ "$RC" -ne 0 ]; then
  echo "  --- offending files ---"
  grep -oE '^[^:]+' /tmp/p18_1_fmt.log | sort -u | head -20 | sed 's/^/    /'
fi
exit $RC
