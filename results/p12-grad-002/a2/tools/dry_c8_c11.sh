#!/usr/bin/env bash
# A2 dry-run of C8 (printed numbers) and C11(b) (generated outputs): cur vs nograd.
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/a2/tools
L=$T/../logs
SUF=${1:-dry}
python3 $T/compare_ctest_numbers.py $HOME/g2/cur/ctest_verbose.txt $HOME/g2/nograd/ctest_verbose.txt > $L/${SUF}_c8_numbers.log 2>&1; echo "exit $?" >> $L/${SUF}_c8_numbers.log
python3 $T/compare_outputs.py $HOME/g2/cur/src_tree $HOME/g2/nograd/src_tree > $L/${SUF}_c11b_outputs.log 2>&1; echo "exit $?" >> $L/${SUF}_c11b_outputs.log
tail -3 $L/${SUF}_c8_numbers.log; tail -3 $L/${SUF}_c11b_outputs.log
python3 $T/c8_categorize.py $L/${SUF}_c8_numbers.log > $L/${SUF}_c8_categories.log 2>&1; echo "exit $?" >> $L/${SUF}_c8_categories.log
python3 $T/c11b_fields.py $HOME/g2/cur/src_tree $HOME/g2/nograd/src_tree $L/${SUF}_c11b_outputs.log > $L/${SUF}_c11b_fields.log 2>&1; echo "exit $?" >> $L/${SUF}_c11b_fields.log
tail -2 $L/${SUF}_c8_categories.log; grep -E "FIELD|\?\?|C11" $L/${SUF}_c11b_fields.log
