#include <cstdio>
#include "cfd/mesh/MeshFingerprint.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
int main() {
  using cfd::mesh::MeshGeometry;
  std::printf("createCartesian2D(4, 3, 1.0, 1.0)   %s\n", cfd::mesh::computeMeshFingerprint(MeshGeometry::createCartesian2D(4, 3, 1.0, 1.0)).c_str());
  std::printf("createCartesian2D(13, 7, 2.1, 0.9)  %s\n", cfd::mesh::computeMeshFingerprint(MeshGeometry::createCartesian2D(13, 7, 2.1, 0.9)).c_str());
}
