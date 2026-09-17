#!/usr/bin/env bash
# P12-MESH-005 finding: explicit diffusion() with two cells along a boundary normal -- BASE vs final.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BT=$HOME/m5ref/base
L=$R/results/p12-mesh-005/logs/04_diffusion_two_cell_finding.log
mkdir -p $HOME/m5probe
build() { # tree builddir out
  c++ -std=c++20 -O3 -DNDEBUG -I$1/include -I$2/generated/include -I$2/_deps/nlohmann_json-src/include \
    $R/results/p12-mesh-005/tools/diffusion_n2_probe.cpp $2/src/libcfdcore.a -o $3
}
build $BT $BT/build $HOME/m5probe/diffn2_base || exit 1
build $R $R/build/release $HOME/m5probe/diffn2_new || exit 1
{
  echo "# P12-MESH-005 finding (pre-existing, dimension-independent): explicit diffusion() with exactly two cells"
  echo "# along a boundary normal. Probe tools/diffusion_n2_probe.cpp (2D API only), compiled against"
  echo "#   BASE = pre-MESH-005 snapshot $BT (lib sha256 $(sha256sum $BT/build/src/libcfdcore.a | cut -c1-16))"
  echo "#   NEW  = final build/release     (lib sha256 $(sha256sum $R/build/release/src/libcfdcore.a | cut -c1-16)); $(date -u)"
  echo "## BASE"; $HOME/m5probe/diffn2_base > $HOME/m5probe/diffn2_base.txt; cat $HOME/m5probe/diffn2_base.txt
  echo "## NEW";  $HOME/m5probe/diffn2_new  > $HOME/m5probe/diffn2_new.txt;  cat $HOME/m5probe/diffn2_new.txt
  if cmp -s $HOME/m5probe/diffn2_base.txt $HOME/m5probe/diffn2_new.txt; then
    echo "VERDICT: BASE and NEW output byte-identical (17 significant digits per cell) -> the n = 2 defect is pre-existing and unchanged by MESH-005"
  else
    echo "VERDICT: BASE and NEW DIFFER"
  fi
  echo "# The 3D analogue (createCartesian3D(2, 4, 4), phi = x^2 + y^2 + z^2) is in logs/03 (same max error 2.167)."
} > $L 2>&1
cat $L
