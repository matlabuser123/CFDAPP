#!/usr/bin/env bash
echo "--- find ninja/cmake ---"
ls -la /usr/bin/ninja /usr/local/bin/ninja /snap/bin/ninja 2>/dev/null
find / -maxdepth 4 -name 'ninja' -type f 2>/dev/null | head -10
echo "--- cmake ---"
find / -maxdepth 4 -name 'cmake' -type f 2>/dev/null | head -10
echo "--- nvcc ---"
ls /usr/local/cuda-12.9/bin/nvcc 2>/dev/null
echo "--- home dotfiles ---"
ls -a ~ | head -30
