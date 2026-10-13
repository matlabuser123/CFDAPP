#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "### the long pole: slowest tests set the floor for any -j ###"
for b in final debug; do
  f=build/$b/Testing/Temporary/CTestCostData.txt
  [ -f "$f" ] || continue
  echo "  build/$b top 6 by cost:"
  sort -k3 -g -r "$f" 2>/dev/null | head -6 | awk '{printf "    %-62s %7.1f s\n", $1, $3}'
  echo "    total serial cost: $(awk '{s+=$3} END{printf "%.0f s (%.1f min)", s, s/60}' "$f")"
done
echo
echo "### memory headroom while the current -j4 run is live ###"
free -h | sed 's/^/  /'
echo
echo "### current debug ctest progress ###"
echo "  completed: $(grep -ac 'Passed\|Failed' results/p15/thermal-energy/logs/ctest_debug_gui.log 2>/dev/null)"
echo "  live test processes: $(ps aux 2>/dev/null | grep -c '[C]FD.*Tests')"
echo "  loadavg: $(cut -d' ' -f1-3 /proc/loadavg)"
