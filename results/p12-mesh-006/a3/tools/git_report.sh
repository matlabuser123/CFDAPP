#!/usr/bin/env bash
# P12-MESH-006 final git report (read-only; nothing is staged, committed or pushed).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$R/results/p12-mesh-006/a3/logs
cd $R
{
  echo "# P12-MESH-006 final git report, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "## git rev-parse HEAD"
  git rev-parse HEAD
  echo "## git log -1 --oneline"
  git log -1 --oneline
  echo "## git status --short ($(git status --short | wc -l) entries: $(git status --short | grep -c '^ M') modified, $(git status --short | grep -c '^??') untracked, $(git status --short | grep -vcE '^( M|\?\?)') other)"
  git status --short
  echo "## git diff --stat"
  git diff --stat
  echo "## git diff --check"
  git diff --check
  echo "git diff --check exit $?"
  echo "## staged changes (git diff --cached --stat): $(git diff --cached --stat | wc -l) lines"
  echo "## remote: $(git rev-parse origin/main 2>/dev/null) (origin/main, local ref)"
} > $L/22_git_report.log 2>&1
grep -E "^## git status|^git diff --check exit|^## staged|^## remote|files changed" $L/22_git_report.log
sed -n '2,5p' $L/22_git_report.log
