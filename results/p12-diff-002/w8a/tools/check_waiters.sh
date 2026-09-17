#!/usr/bin/env bash
for p in 92963 96544 98237; do
  if [ -d "/proc/$p" ]; then
    echo "$p alive: $(tr '\0' ' ' < "/proc/$p/cmdline" | cut -c1-140)"
  else
    echo "$p gone"
  fi
done
