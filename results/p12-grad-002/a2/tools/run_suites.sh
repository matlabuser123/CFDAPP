#!/usr/bin/env bash
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/a2/tools
bash $T/run_suite.sh cur 6 > /dev/null &
bash $T/run_suite.sh nograd 6 > /dev/null &
wait
cat $T/../logs/suite_cur.log $T/../logs/suite_nograd.log
