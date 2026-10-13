#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/build/final
./tests/unit/transport/CFDTransportTests 2>&1 | tail -45
