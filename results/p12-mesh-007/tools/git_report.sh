#!/usr/bin/env bash
# P12-MESH-007 git report at the stop (read-only; nothing staged, committed or pushed).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$R/results/p12-mesh-007/logs/18_git_report.log
cd $R
{
  echo "# P12-MESH-007 git report, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "## git rev-parse HEAD: $(git rev-parse HEAD)"
  echo "## origin/main (local ref): $(git rev-parse origin/main)"
  echo "## staged changes: $(git diff --cached --name-only | wc -l)"
  echo "## git status --short: $(git status --short | wc -l) entries ($(git status --short | grep -c '^ M') modified, $(git status --short | grep -c '^??') untracked)"
  git status --short
  echo "## delta vs the pre-MESH-007 baseline status (logs/00_baseline_git.log)"
  awk '/^\$ git status --short$/{f=1; next} f && /^( M|\?\?|A |D ) /' results/p12-mesh-007/logs/00_baseline_git.log | sort > $HOME/m7_status_base.txt
  git status --short | sort > $HOME/m7_status_now.txt
  echo "# new since the baseline:"; comm -13 $HOME/m7_status_base.txt $HOME/m7_status_now.txt
  echo "# gone since the baseline:"; comm -23 $HOME/m7_status_base.txt $HOME/m7_status_now.txt
  echo "## tracked files changed by MESH-007 that were already modified before it (content vs BASE):"
  for f in $(git diff --name-only); do
    if [ -f $HOME/m7ref/base/$f ] && ! cmp -s $f $HOME/m7ref/base/$f; then echo "  $f"; fi
  done
  echo "## git diff --stat (all uncommitted P12-MESH-001..007 work)"
  git diff --stat | tail -1
  echo "## git diff --check"
  git diff --check
  echo "git diff --check exit $?"
} > $L 2>&1
cat $L | grep -vE "^ M |^\?\? "
grep -A40 "new since the baseline" $L | head -40
