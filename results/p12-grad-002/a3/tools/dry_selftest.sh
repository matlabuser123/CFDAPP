#!/usr/bin/env bash
# A3 pre-freeze dry-run of C10-A3(d) on the A2 PRE-FREEZE dumps ($HOME/g2/c10, repo vs nograd).
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/a3
ls -la $HOME/g2/c10/dump_repo.txt $HOME/g2/c10/dump_nograd.txt > $T/logs/dry_selftest.log 2>&1
python3 $T/tools/selftest_c10.py $HOME/g2/c10/dump_repo.txt $HOME/g2/c10/dump_nograd.txt >> $T/logs/dry_selftest.log 2>&1
echo "exit $?" >> $T/logs/dry_selftest.log
cat $T/logs/dry_selftest.log
