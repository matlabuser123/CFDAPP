#!/usr/bin/env bash
# Freeze the Release CFDCaseIntegrationTests binary for the long G5/G6/G7 gate runs.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
mkdir -p $HOME/m6gate
cp $R/build/release/tests/integration/case/CFDCaseIntegrationTests $HOME/m6gate/
sha256sum $R/build/release/tests/integration/case/CFDCaseIntegrationTests $HOME/m6gate/CFDCaseIntegrationTests
