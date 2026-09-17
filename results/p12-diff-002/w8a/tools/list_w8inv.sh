#!/usr/bin/env bash
ps -eo pid,etimes,pcpu,args | grep '[w]8inv' | cut -c1-160
