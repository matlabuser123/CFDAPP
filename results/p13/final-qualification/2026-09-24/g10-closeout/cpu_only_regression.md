# G10 — CPU-only regression

```text
### cpu_only  (build/qual-cpuonly)
############################################################
  CFDAPP_BUILD_GUI:BOOL=OFF
  CFDAPP_ENABLE_CUDA:BOOL=OFF
  CFDAPP_ENABLE_OPENMP:BOOL=ON
  CMAKE_BUILD_TYPE:STRING=Release

--- build ---
  build exit: 0   (81 s)

--- OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 ---
  ctest exit status : 0
  total tests       : 2288
```

Full log: `logs/G10_cpu_only_ctest.log`  ·  build log: `logs/G10_cpu_only_build.log`

Invocation: `OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 -E <5 documented exclusions>`
