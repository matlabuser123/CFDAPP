// P16 Step 5 -- CUDA kernel launch tracer.
//
// WHY THIS EXISTS
// ---------------
// The amended CUDA diagnostics gate replaces an exhaustive sanitizer sweep
// with a coverage-driven scoped set. That trade is only honest if the coverage
// is MEASURED. Asserting "the long channel benchmarks launch no kernel the
// short tests miss" is exactly the kind of argument this phase has repeatedly
// found to be wrong.
//
// Neither nsys, ncu nor CUPTI is installed on this machine, and
// compute-sanitizer filters kernels but will not enumerate them. So this
// interposes cudaLaunchKernel via LD_PRELOAD and resolves each launch to a
// kernel name with cudaFuncGetName (an official CUDA 12.3+ API, not symbol
// table guesswork).
//
// It changes NOTHING in CFDApp: no production source is touched, no rebuild of
// the project is required, and the traced binary is the same one the gate runs.
//
// Build:
//   gcc -shared -fPIC -o kernel_tracer.so kernel_tracer.c -ldl
// Use:
//   CFD_KERNEL_TRACE=<out.txt> LD_PRELOAD=./kernel_tracer.so <binary> <args>
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// dim3 is a C++ type; this file is C, so the layout is restated. Only the
// values are forwarded untouched, so the exact fields never matter here.
typedef struct {
  unsigned int x, y, z;
} dim3_stub;

typedef int (*launch_fn)(const void*, dim3_stub, dim3_stub, void**, size_t, void*);

typedef int (*func_get_name_fn)(const char**, const void*);

static FILE* trace_file = NULL;
static func_get_name_fn get_name = NULL;

// A small set so a kernel launched a million times is written once. The gate
// asks which kernels ran, not how often.
#define MAX_SEEN 512
static const void* seen[MAX_SEEN];
static int seen_count = 0;

static int already_seen(const void* f) {
  for (int i = 0; i < seen_count; ++i) {
    if (seen[i] == f) return 1;
  }
  if (seen_count < MAX_SEEN) seen[seen_count++] = f;
  return 0;
}

int cudaLaunchKernel(const void* func, dim3_stub grid, dim3_stub block, void** args,
                     size_t shared, void* stream) {
  static launch_fn real = NULL;
  if (real == NULL) real = (launch_fn)dlsym(RTLD_NEXT, "cudaLaunchKernel");

  if (trace_file == NULL) {
    const char* path = getenv("CFD_KERNEL_TRACE");
    if (path != NULL) trace_file = fopen(path, "a");
    if (get_name == NULL) get_name = (func_get_name_fn)dlsym(RTLD_NEXT, "cudaFuncGetName");
  }

  if (trace_file != NULL && !already_seen(func)) {
    const char* name = NULL;
    if (get_name != NULL && get_name(&name, func) == 0 && name != NULL) {
      fprintf(trace_file, "%s\n", name);
    } else {
      // Fail LOUD: an unresolved launch must not be silently omitted from a
      // coverage matrix, because a missing row reads as "not launched".
      fprintf(trace_file, "UNRESOLVED_KERNEL_%p\n", func);
    }
    fflush(trace_file);
  }

  return real(func, grid, block, args, shared, stream);
}
