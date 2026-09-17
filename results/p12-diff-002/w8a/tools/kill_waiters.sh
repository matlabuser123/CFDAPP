#!/usr/bin/env bash
kill 92963 96544 98237 2>&1
sleep 1
for p in 92963 96544 98237; do [ -d "/proc/$p" ] && echo "$p still alive" || echo "$p terminated"; done
