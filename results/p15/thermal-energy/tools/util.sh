#!/usr/bin/env bash
echo "  live test procs : $(ps aux 2>/dev/null | grep -c '[C]FD.*Tests')"
echo "  loadavg         : $(cut -d' ' -f1-3 /proc/loadavg)"
free -m | awk '/^Mem:/{printf "  mem             : %s MiB used, %s MiB available\n", $3, $7}'
free -m | awk '/^Swap:/{printf "  swap            : %s MiB used\n", $3}'
echo "  procs in D (io-wait): $(ps -eo stat 2>/dev/null | grep -c '^D')"
echo "  repo filesystem : $(df -T /mnt/c/Users/Hasib/Desktop/CFDAPP 2>/dev/null | tail -1 | awk '{print $2}')"
