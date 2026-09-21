// GPU-DISC-001A gate -- verify DeviceMesh against the CPU mesh.
//
// Every attribute is downloaded back and compared to the host Mesh value it was
// built from. Geometry is copied, not recomputed, so the requirement is BITWISE
// equality: any difference is a layout or indexing defect, not rounding. The one
// derived quantity, face area magnitude, is taken from Face::area() on the host
// and so must also match bitwise.
//
// The probe is proven non-vacuous at the end: a deliberately corrupted copy of
// the expected data must be detected by the same comparison.
//
// usage: mesh_equivalence

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cfd/gpu/DeviceMesh.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::gpu::DeviceMesh;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

int failures = 0;
int checks = 0;

// Pull a device array back to the host. DeviceMesh exposes raw device pointers,
// so the copy is done here rather than adding a download path to production
// code that production code would never use.
template <typename T>
std::vector<T> download(const T* devicePtr, Index count) {
  std::vector<T> host(static_cast<std::size_t>(count));
  if (count > 0) {
    cudaMemcpy(host.data(), devicePtr, static_cast<std::size_t>(count) * sizeof(T),
               cudaMemcpyDeviceToHost);
  }
  return host;
}

template <typename T>
void expectExact(const std::string& what, const std::vector<T>& got, const std::vector<T>& want) {
  ++checks;
  if (got.size() != want.size()) {
    ++failures;
    std::printf("    FAIL %-28s size %zu != %zu\n", what.c_str(), got.size(), want.size());
    return;
  }
  for (std::size_t i = 0; i < got.size(); ++i) {
    if (std::memcmp(&got[i], &want[i], sizeof(T)) != 0) {
      ++failures;
      std::printf("    FAIL %-28s first difference at index %zu\n", what.c_str(), i);
      return;
    }
  }
  std::printf("    ok   %-28s %zu values, bitwise\n", what.c_str(), got.size());
}

void checkMesh(const std::string& label, const Mesh& mesh) {
  DeviceMesh dm;
  const auto before = cfd::gpu::gpuExecutionStats().allocations;
  dm.upload(mesh);
  const auto after = cfd::gpu::gpuExecutionStats().allocations;

  std::printf("\n=== %s: %zu cells, %zu faces, %dD ===\n", label.c_str(), mesh.numberOfCells(),
              mesh.numberOfFaces(), mesh.dimension());
  std::printf("    resident %.1f kB, allocations for this upload: %llu\n",
              dm.residentBytes() / 1024.0, (unsigned long long)(after - before));

  const Index nc = dm.cellCount();
  const Index nf = dm.faceCount();
  ++checks;
  if (nc != mesh.numberOfCells() || nf != mesh.numberOfFaces()) {
    ++failures;
    std::printf("    FAIL counts: device %zu/%zu host %zu/%zu\n", nc, nf, mesh.numberOfCells(),
                mesh.numberOfFaces());
    return;
  }

  // --- expected values, straight from the host mesh ------------------------
  std::vector<Real> volume(nc), cx(nc), cy(nc), cz(nc);
  std::vector<Index> faceOffsets(nc + 1, 0), cellFaces;
  std::size_t running = 0;
  for (Index c = 0; c < nc; ++c) {
    const auto& cell = mesh.cell(c);
    volume[c] = cell.volume();
    cx[c] = cell.centroid().x;
    cy[c] = cell.centroid().y;
    cz[c] = cell.centroid().z;
    faceOffsets[c] = static_cast<Index>(running);
    running += cell.faceIds().size();
    for (const Index f : cell.faceIds()) cellFaces.push_back(f);
  }
  faceOffsets[nc] = static_cast<Index>(running);

  std::vector<Index> owner(nf), neighbor(nf);
  std::vector<Real> fcx(nf), fcy(nf), fcz(nf), sx(nf), sy(nf), sz(nf), area(nf);
  for (Index f = 0; f < nf; ++f) {
    const auto& face = mesh.face(f);
    owner[f] = face.owner();
    neighbor[f] = face.neighbor().has_value() ? *face.neighbor() : DeviceMesh::kNoNeighbor;
    fcx[f] = face.centroid().x;
    fcy[f] = face.centroid().y;
    fcz[f] = face.centroid().z;
    sx[f] = face.areaVector().x;
    sy[f] = face.areaVector().y;
    sz[f] = face.areaVector().z;
    area[f] = face.area();
  }

  expectExact("cell volumes", download(dm.cellVolumes(), nc), volume);
  expectExact("cell centroid x", download(dm.cellCentroidX(), nc), cx);
  expectExact("cell centroid y", download(dm.cellCentroidY(), nc), cy);
  expectExact("cell centroid z", download(dm.cellCentroidZ(), nc), cz);
  expectExact("face owner", download(dm.faceOwner(), nf), owner);
  expectExact("face neighbour", download(dm.faceNeighbor(), nf), neighbor);
  expectExact("face centroid x", download(dm.faceCentroidX(), nf), fcx);
  expectExact("face centroid y", download(dm.faceCentroidY(), nf), fcy);
  expectExact("face centroid z", download(dm.faceCentroidZ(), nf), fcz);
  expectExact("face area vector x", download(dm.faceAreaX(), nf), sx);
  expectExact("face area vector y", download(dm.faceAreaY(), nf), sy);
  expectExact("face area vector z", download(dm.faceAreaZ(), nf), sz);
  expectExact("face area magnitude", download(dm.faceArea(), nf), area);
  expectExact("cell-face offsets", download(dm.cellFaceOffsets(), nc + 1), faceOffsets);
  expectExact("cell-face ids", download(dm.cellFaceIds(), static_cast<Index>(cellFaces.size())),
              cellFaces);

  // --- boundary patches ----------------------------------------------------
  std::vector<Index> boundaryFaces;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index f : patch.faceIds()) boundaryFaces.push_back(f);
  }
  expectExact("boundary face ids",
              download(dm.boundaryFaceIds(), static_cast<Index>(boundaryFaces.size())),
              boundaryFaces);

  ++checks;
  bool patchesOk = dm.patches().size() == mesh.boundaryPatches().size();
  if (patchesOk) {
    Index expectedFirst = 0;
    for (std::size_t p = 0; p < dm.patches().size(); ++p) {
      const auto& dp = dm.patches()[p];
      const auto& hp = mesh.boundaryPatches()[p];
      if (dp.name != hp.name() || dp.faceCount != static_cast<Index>(hp.faceIds().size()) ||
          dp.firstFace != expectedFirst) {
        patchesOk = false;
        break;
      }
      expectedFirst += dp.faceCount;
    }
  }
  if (!patchesOk) {
    ++failures;
    std::printf("    FAIL boundary patch slices do not match the host mesh\n");
  } else {
    std::printf("    ok   %-28s %zu patches, names and slices\n", "boundary patches",
                dm.patches().size());
  }

  // --- re-upload must not reallocate ---------------------------------------
  const auto reallocBefore = cfd::gpu::gpuExecutionStats().reallocations;
  dm.upload(mesh);
  const auto reallocAfter = cfd::gpu::gpuExecutionStats().reallocations;
  ++checks;
  if (reallocAfter != reallocBefore) {
    ++failures;
    std::printf("    FAIL re-uploading the same mesh reallocated (%llu times)\n",
                (unsigned long long)(reallocAfter - reallocBefore));
  } else {
    std::printf("    ok   %-28s 0 reallocations\n", "re-upload reuses buffers");
  }
  std::fflush(stdout);
}

}  // namespace

int main() {
  if (!cfd::gpu::cudaAvailable()) {
    std::printf("CUDA unavailable -- cannot run\n");
    return 77;
  }

  checkMesh("Cartesian 2D 8x8", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  checkMesh("Cartesian 2D 40x40", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0));
  checkMesh("Cartesian 2D non-square 32x8", MeshGeometry::createCartesian2D(32, 8, 4.0, 1.0));
  checkMesh("Cartesian 3D 6x6x6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0));
  checkMesh("Cartesian 3D 12x8x4", MeshGeometry::createCartesian3D(12, 8, 4, 3.0, 2.0, 1.0));

  // --- non-vacuity ---------------------------------------------------------
  // The comparison must be able to fail. Corrupt one value of an otherwise
  // correct expectation and require expectExact to report it.
  std::printf("\n=== probe non-vacuity ===\n");
  const int beforeFailures = failures;
  std::vector<Real> a{1.0, 2.0, 3.0};
  std::vector<Real> b{1.0, 2.0, 3.0000000000000004};  // one ulp apart
  expectExact("injected one-ulp difference", a, b);
  const bool detected = failures == beforeFailures + 1;
  if (detected) {
    std::printf("    ok   comparison detects a one-ulp difference\n");
    failures = beforeFailures;  // the injected failure was expected
  } else {
    std::printf("    FAIL comparison is VACUOUS -- it cannot detect a wrong value\n");
    failures = beforeFailures + 1;
  }

  std::printf("\nchecks: %d   failures: %d\n", checks, failures);
  std::printf("%s\n", failures ? "DEVICE MESH: FAIL" : "DEVICE MESH: PASS");
  return failures ? 1 : 0;
}
