#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "### repository size (decides clone cost) ###"
echo "  tracked files      : $(git ls-files | wc -l)"
echo "  .git size          : $(du -sh .git 2>/dev/null | cut -f1)"
echo "  working tree (no build/): $(du -sh --exclude=build --exclude=.git . 2>/dev/null | cut -f1)"
echo "  build dirs         : $(du -sh build 2>/dev/null | cut -f1)"
echo
echo "### uncommitted P14+P15 work that a clone would NOT carry ###"
echo "  modified   : $(git status --porcelain | grep -c '^ M')"
echo "  untracked  : $(git status --porcelain | grep -c '^??')"
echo "  HEAD       : $(git rev-parse --short=12 HEAD)"
echo
echo "### ccache installability ###"
echo "  apt-cache policy ccache:"
apt-cache policy ccache 2>/dev/null | head -3 | sed 's/^/    /'
echo "  passwordless sudo: $(sudo -n true 2>/dev/null && echo yes || echo 'NO -- needs a password')"
echo
echo "### timing baseline on 9p: git status ###"
/usr/bin/time -f "    git status: %e s" git status --porcelain > /dev/null
