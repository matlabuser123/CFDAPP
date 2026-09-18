# CUDA-QUAL-001: activate the qualified toolkit for a shell or script.
#   . results/cuda-qual-001/tools/env.sh
# Deliberately explicit rather than relying on a login shell: the project's
# builds run through non-interactive `wsl.exe -- bash script.sh`.
export CUDA_HOME=/usr/local/cuda-12.9
export PATH=$CUDA_HOME/bin:$PATH
export LD_LIBRARY_PATH=$CUDA_HOME/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export CUDACXX=$CUDA_HOME/bin/nvcc
