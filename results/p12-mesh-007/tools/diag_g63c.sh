#!/usr/bin/env bash
# P12-MESH-007 G6.3 diagnosis part 3: the same static-translation program compiled against BASE (the
# pre-MESH-007 library, $HOME/m7ref/base) and NEW (build/release). Evidence only.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BT=$HOME/m7ref/base
W=$HOME/m7diag; mkdir -p $W
SRC=$R/results/p12-mesh-007/tools/diag_g63c_static.cpp
L=$R/results/p12-mesh-007/logs/15_diag_g63_static_translation_BASE_and_NEW.log
c++ -std=c++20 -O3 -DNDEBUG -I$BT/include -I$BT/build/generated/include -I$BT/build/_deps/nlohmann_json-src/include \
  $SRC $BT/build/src/libcfdcore.a -o $W/diag_c_base || exit 1
c++ -std=c++20 -O3 -DNDEBUG -I$R/include -I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include \
  $SRC $R/build/release/src/libcfdcore.a -o $W/diag_c_new || exit 1
{
  echo "# P12-MESH-007 G6.3 diagnosis part 3 (static translation, pre-MESH-007 APIs only); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "## BASE (pre-MESH-007 library, lib $(sha256sum $BT/build/src/libcfdcore.a | cut -c1-16))"
  $W/diag_c_base
  echo "## NEW (build/release, lib $(sha256sum $R/build/release/src/libcfdcore.a | cut -c1-16))"
  $W/diag_c_new
  echo "## outputs identical: $(cmp -s <($W/diag_c_base) <($W/diag_c_new) && echo yes || echo NO)"
} > $L 2>&1
cat $L
