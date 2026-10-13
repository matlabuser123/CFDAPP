#!/usr/bin/env python3
"""Emit the distinct existing test executables from `ctest --show-only=json-v1` on stdin.

Split out of the regression harness so the harness itself contains no embedded Python. Embedding it
as a heredoc-inside-a-heredoc is what corrupted the first attempt at that script: the inner quoting
did not survive, and the file was never written at all.

Exit codes follow the harness's original inline version: 3 if the JSON cannot be parsed, 4 if no
test commands were found. Both are failures, because an empty binary list would make the staleness
check that consumes it pass vacuously.
"""
import json
import os
import sys

try:
    doc = json.load(sys.stdin)
except Exception:
    sys.exit(3)

seen = set()
for test in doc.get("tests", []):
    command = test.get("command") or []
    if not command:
        continue
    exe = command[0]
    if exe in seen:
        continue
    seen.add(exe)
    if os.path.isfile(exe):
        print(exe)

if not seen:
    sys.exit(4)
