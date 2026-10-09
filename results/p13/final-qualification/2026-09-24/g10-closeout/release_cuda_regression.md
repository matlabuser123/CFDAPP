# G10 — Release+CUDA regression

```text
### release_cuda  (build/qual-final)
############################################################
  CFDAPP_BUILD_GUI:BOOL=OFF
  CFDAPP_ENABLE_CUDA:BOOL=ON
  CFDAPP_ENABLE_OPENMP:BOOL=ON
  CMAKE_BUILD_TYPE:STRING=Release

--- build ---
  build exit: 0   (3 s)

--- OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 ---
  ctest exit status : 0
  total tests       : 2488
```

Full log: `logs/G10_release_cuda_ctest.log`  ·  build log: `logs/G10_release_cuda_build.log`

Invocation: `OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 -E <5 documented exclusions>`
