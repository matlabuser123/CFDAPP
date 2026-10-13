#!/usr/bin/env bash
echo "loadavg now : $(cat /proc/loadavg)"
echo "cores       : $(nproc)"
