#!/usr/bin/env bash
# Plan-level unsupportedReason() for a convective wall: criteria 3-5.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cat > /tmp/p15_plan_reason.cpp <<'CPP'
#include <cstdio>
#include <exception>
#include <string>
#include <memory>
#include "cfd/boundary/ConvectiveHeatTransfer.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/gpu/DeviceConvection.hpp"
#include "cfd/gpu/DeviceDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
int main() {
  const auto mesh = cfd::mesh::MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  cfd::boundary::BoundaryConditionSet bcs;
  bcs.set(mesh, "left", std::make_unique<cfd::boundary::FixedTemperature>(400.0));
  bcs.set(mesh, "right", std::make_unique<cfd::boundary::ConvectiveHeatTransfer>(25.0, 300.0));
  bcs.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedTemperature>(350.0));
  bcs.set(mesh, "top", std::make_unique<cfd::boundary::FixedTemperature>(350.0));

  int failures = 0;
  cfd::gpu::DeviceDiffusionPlan diffusion;
  bool threw = false;
  bool usable = true;
  try { usable = diffusion.build(mesh, bcs); } catch (const std::exception& e) { threw = true; std::printf("  EXCEPTION: %s\n", e.what()); }
  std::printf("diffusion plan: threw=%d usable=%d\n  reason: %s\n",
              threw ? 1 : 0, usable ? 1 : 0, diffusion.unsupportedReason().c_str());
  if (threw) { std::printf("  FAIL: construction threw\n"); ++failures; }
  if (usable) { std::printf("  FAIL: not classified unsupported\n"); ++failures; }
  const std::string dr = diffusion.unsupportedReason();
  if (dr.empty()) { std::printf("  FAIL: reason empty\n"); ++failures; }
  if (dr.find("ConvectiveHeatTransfer") == std::string::npos) {
    std::printf("  FAIL: reason does not name the condition\n"); ++failures; }
  if (dr.find("conductivity") == std::string::npos) {
    std::printf("  FAIL: reason does not name runtime conductivity\n"); ++failures; }
  // Added after the first run of this gate. The generic message it found
  // claimed the condition is "not bitwise-reproducible as ... an affine
  // function of phi_P", which is FALSE -- a Robin wall IS affine in T_P. A
  // refusal that misstates its cause sends the user to fix a boundary
  // condition that is not broken.
  if (dr.find("not bitwise-reproducible") != std::string::npos) {
    std::printf("  FAIL: reason still claims the condition is not representable\n"); ++failures; }

  cfd::gpu::DeviceConvectionPlan convection;
  threw = false; usable = true;
  try { usable = convection.build(mesh, bcs); } catch (const std::exception& e) { threw = true; std::printf("  EXCEPTION: %s\n", e.what()); }
  std::printf("convection plan: threw=%d usable=%d\n  reason: %s\n",
              threw ? 1 : 0, usable ? 1 : 0, convection.unsupportedReason().c_str());
  if (threw) { std::printf("  FAIL: construction threw\n"); ++failures; }
  if (usable) { std::printf("  FAIL: not classified unsupported\n"); ++failures; }
  const std::string cr = convection.unsupportedReason();
  if (cr.find("ConvectiveHeatTransfer") == std::string::npos ||
      cr.find("conductivity") == std::string::npos ||
      cr.find("not bitwise-reproducible") != std::string::npos) {
    std::printf("  FAIL: convection reason incomplete\n"); ++failures; }

  std::printf("\n%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
CPP
export PATH=/usr/local/cuda-12.9/bin:$PATH
rm -f /tmp/p15_plan_reason
g++ -std=c++20 -O2 -DNDEBUG -I include -I build/final/generated/include \
  -I /usr/local/cuda-12.9/include /tmp/p15_plan_reason.cpp -o /tmp/p15_plan_reason \
  -Wl,--start-group build/final/src/libcfdcore.a build/final/cuda/libcfdcuda.a -Wl,--end-group \
  -L/usr/local/cuda-12.9/lib64 -lcudart 2>&1 | head -10
[ -x /tmp/p15_plan_reason ] || { echo "COMPILE FAILED"; exit 1; }
/tmp/p15_plan_reason
