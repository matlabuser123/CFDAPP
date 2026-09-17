#!/usr/bin/env bash
# P12-MESH-006 final integrity check, after the documentation closeout (read-only):
#   1. the original gate and the original G5/G6 evidence vs the hash list taken at the start of the
#      investigation (g5-investigation/logs/original_evidence_sha256_start.txt);
#   2. the A3 freeze list (a3/logs/00_freeze.log): the A3 document, the frozen expected values, the
#      evaluator, the run scripts, the investigation tools used, and acceptance_gate.md;
#   3. frozen_expected.json vs its recorded .sha256;
#   4. g5-investigation/: file count and newest modification time;
#   5. clang-format-18 over include/ src/ apps/ tests/ (the CI format job's scope), final.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-mesh-006
L=$P/a3/logs
cd $P
{
  echo "# P12-MESH-006 final integrity check, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "## 1. original gate + original G5/G6 evidence vs the investigation's start list"
  grep -v '^#' g5-investigation/logs/original_evidence_sha256_start.txt | sha256sum -c
  echo "rc=$?"
  echo "## 2. A3 freeze list (frozen 2026-09-15T12:36:10Z)"
  grep -E '^[0-9a-f]{64} \*' a3/logs/00_freeze.log | sha256sum -c
  echo "rc=$?"
  echo "## 3. frozen_expected.json vs its recorded .sha256"
  (cd a3/data && sha256sum -c frozen_expected.json.sha256)
  echo "rc=$?"
  echo "## 4. g5-investigation/"
  echo "files: $(find g5-investigation -type f | wc -l)"
  echo "newest (UTC): $(TZ=UTC find g5-investigation -type f -printf '%TY-%Tm-%TdT%TH:%TM:%.2TSZ %p\n' | sort | tail -1)"
  echo "## 5. clang-format"
  bash $P/a3/tools/format_check.sh
} > $L/21_final_integrity.log 2>&1
grep -vE ': OK$' $L/21_final_integrity.log
