#!/usr/bin/env bash
# P12-MESH-006: clang-format-18 dry run over include/ src/ apps/ tests/ (the CI format job's scope),
# listing every file that would change. Usage: format_check.sh [--apply <file>...]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd $R
if [ "${1:-}" = "--apply" ]; then
  shift
  clang-format-18 -i "$@"
  echo "formatted $# files"
  exit 0
fi
total=0; bad=0
while IFS= read -r f; do
  total=$((total + 1))
  if ! clang-format-18 --dry-run --Werror "$f" > /dev/null 2>&1; then
    bad=$((bad + 1)); echo "WOULD CHANGE $f"
  fi
done < <(find include src apps tests -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cu' -o -name '*.cuh' \) | sort)
echo "clang-format-18 --dry-run --Werror: $bad of $total files would change"
