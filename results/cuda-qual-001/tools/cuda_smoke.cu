// CUDA-QUAL-001 step 3: an independent device-query + kernel smoke test, with no CFDApp code.
// The kernel reports the __CUDA_ARCH__ it was compiled for, so the output distinguishes native
// SASS for the device's own architecture from PTX JIT-compiled from an older virtual arch.
#include <cstdio>
#include <cuda_runtime.h>

#define CK(call)                                                                      \
  do {                                                                                \
    cudaError_t e = (call);                                                           \
    if (e != cudaSuccess) {                                                           \
      std::printf("FAIL %s:%d %s -> %s\n", __FILE__, __LINE__, #call,                 \
                  cudaGetErrorString(e));                                             \
      return 1;                                                                       \
    }                                                                                 \
  } while (0)

__global__ void saxpyAndReportArch(const double a, const double* x, const double* y, double* out,
                                   int* arch, int n) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i == 0) {
#ifdef __CUDA_ARCH__
    *arch = __CUDA_ARCH__;
#else
    *arch = -1;
#endif
  }
  if (i < n) out[i] = a * x[i] + y[i];
}

int main() {
  int driver = 0, runtime = 0, count = 0;
  CK(cudaDriverGetVersion(&driver));
  CK(cudaRuntimeGetVersion(&runtime));
  std::printf("driver API version   %d\n", driver);
  std::printf("runtime API version  %d\n", runtime);
  CK(cudaGetDeviceCount(&count));
  std::printf("device count         %d\n", count);
  if (count < 1) { std::printf("FAIL: no CUDA device\n"); return 1; }

  cudaDeviceProp p{};
  CK(cudaGetDeviceProperties(&p, 0));
  std::printf("device 0             %s\n", p.name);
  std::printf("compute capability   %d.%d\n", p.major, p.minor);
  std::printf("global memory        %.0f MiB\n", static_cast<double>(p.totalGlobalMem) / 1048576.0);
  std::printf("multiprocessors      %d\n", p.multiProcessorCount);
  std::printf("uuid                 ");
  for (int i = 0; i < 16; ++i) std::printf("%02x", static_cast<unsigned char>(p.uuid.bytes[i]));
  std::printf("\n");

  const int n = 1 << 16;
  const double a = 2.5;
  double *dx = nullptr, *dy = nullptr, *dout = nullptr;
  int* darch = nullptr;
  CK(cudaMalloc(&dx, n * sizeof(double)));
  CK(cudaMalloc(&dy, n * sizeof(double)));
  CK(cudaMalloc(&dout, n * sizeof(double)));
  CK(cudaMalloc(&darch, sizeof(int)));
  double* hx = new double[n];
  double* hy = new double[n];
  double* hout = new double[n];
  for (int i = 0; i < n; ++i) { hx[i] = i * 0.5; hy[i] = 1.0 - i * 0.25; }
  CK(cudaMemcpy(dx, hx, n * sizeof(double), cudaMemcpyHostToDevice));
  CK(cudaMemcpy(dy, hy, n * sizeof(double), cudaMemcpyHostToDevice));
  saxpyAndReportArch<<<(n + 255) / 256, 256>>>(a, dx, dy, dout, darch, n);
  CK(cudaGetLastError());
  CK(cudaDeviceSynchronize());
  int arch = 0;
  CK(cudaMemcpy(&arch, darch, sizeof(int), cudaMemcpyDeviceToHost));
  CK(cudaMemcpy(hout, dout, n * sizeof(double), cudaMemcpyDeviceToHost));

  int bad = 0;
  double worst = 0.0;
  for (int i = 0; i < n; ++i) {
    const double want = a * hx[i] + hy[i];
    const double err = hout[i] - want;
    if (err != 0.0) { ++bad; if (err > worst || -err > worst) worst = err < 0 ? -err : err; }
  }
  std::printf("kernel __CUDA_ARCH__ %d  (%d0 = sm_%d)\n", arch, arch / 10, arch / 10);
  std::printf("saxpy mismatches     %d of %d (worst |error| %.3e)\n", bad, n, worst);
  CK(cudaFree(dx)); CK(cudaFree(dy)); CK(cudaFree(dout)); CK(cudaFree(darch));
  delete[] hx; delete[] hy; delete[] hout;
  const bool ok = bad == 0 && arch > 0;
  std::printf("SMOKE %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
