#!/usr/bin/env bash
# N5 -- CFDAppCliLogLinesNeverReachStdout must be able to fail. Make the
# wrapper echo stderr as well; the test must then FAIL.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
F=tests/cmake/RunCliStdoutOnly.cmake
cp $F /tmp/RunCliStdoutOnly.cmake.bak
sha256sum $F > /tmp/n5.sha256
printf 'message("${cli_stderr}")\n' >> $F
echo "--- with stderr echoed onto the matched stream (must FAIL) ---"
( cd build/final && ctest -R CFDAppCliLogLinesNeverReachStdout 2>&1 | tail -6 )
cp /tmp/RunCliStdoutOnly.cmake.bak $F
sha256sum -c /tmp/n5.sha256 --quiet && echo "RESTORED (sha256 verified)" || echo "*** RESTORE FAILED ***"
echo "--- restored (must PASS) ---"
( cd build/final && ctest -R CFDAppCliLogLinesNeverReachStdout 2>&1 | tail -4 )
