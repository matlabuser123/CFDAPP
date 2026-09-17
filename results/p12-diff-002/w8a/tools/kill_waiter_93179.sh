#!/usr/bin/env bash
kill 93179 2>&1; sleep 1
[ -d /proc/93179 ] && echo "93179 still alive" || echo "93179 terminated"
