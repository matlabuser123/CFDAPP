#!/usr/bin/env bash
# A2 dry-run of C10/C11(a): dump on repo, nograd and base; compare repo vs nograd and repo vs base.
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/a2/tools
L=$T/../logs
W=$HOME/g2/c10; mkdir -p $W
for tree in repo nograd base; do
  bash $T/run_a2.sh a2_c10 tmp_c10_$tree.log $tree > /dev/null &
done
wait
for tree in repo nograd base; do
  head -3 $L/tmp_c10_$tree.log > $L/dry_c10_dump_$tree.header.log; tail -2 $L/tmp_c10_$tree.log >> $L/dry_c10_dump_$tree.header.log
  echo "dump lines $(wc -l < $L/tmp_c10_$tree.log) sha256 $(sha256sum $L/tmp_c10_$tree.log | cut -d' ' -f1)" >> $L/dry_c10_dump_$tree.header.log
  mv $L/tmp_c10_$tree.log $W/dump_$tree.txt
done
python3 $T/compare_c10.py $W/dump_repo.txt $W/dump_nograd.txt > $L/dry_c10_repo_vs_nograd.log 2>&1; echo "exit $?" >> $L/dry_c10_repo_vs_nograd.log
python3 $T/compare_c10.py $W/dump_repo.txt $W/dump_base.txt > $L/dry_c10_repo_vs_base.log 2>&1; echo "exit $?" >> $L/dry_c10_repo_vs_base.log
python3 $T/compare_c10.py $W/dump_nograd.txt $W/dump_base.txt > $L/dry_c10_nograd_vs_base.log 2>&1; echo "exit $?" >> $L/dry_c10_nograd_vs_base.log
cat $L/dry_c10_dump_*.header.log
cat $L/dry_c10_repo_vs_nograd.log
# instrument self-test (non-vacuity of the comparator): a mutated NEW dump must be flagged
python3 $T/compare_c10.py $W/dump_repo.txt $W/dump_nograd.txt --mutate-new > $L/dry_c10_selftest_mutated.log 2>&1; echo "exit $?" >> $L/dry_c10_selftest_mutated.log
grep -E "depth>5|ALIGNED" $L/dry_c10_selftest_mutated.log | grep -v "aligned" | cut -c1-60,150-330 | head -20
