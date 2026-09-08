#pragma once

#include <string>

#include "cfd/mesh/Mesh.hpp"

namespace cfd::mesh {

// A deterministic fingerprint of a Mesh's numerical topology/geometry --
// Restart-B (TODO.md P2 -- Restart capability): cell-count/face-count
// alone cannot distinguish two different meshes that happen to share
// both counts. Two Mesh instances built the same way (e.g. two separate
// calls to MeshGeometry::createCartesian2D with identical arguments)
// always produce identical fingerprints; two meshes differing in any
// cell centroid/volume, face owner/neighbor/centroid/area vector, or
// boundary-patch membership produce (overwhelmingly likely) different
// ones.
//
// Computed from, in this exact deterministic order:
//   - every cell's centroid and volume, iterated in cell-id order
//   - every face's owner, neighbor (a fixed sentinel for a boundary
//     face's absent neighbor), centroid, and area vector, iterated in
//     face-id order
//   - every boundary patch's name and face-id list, iterated in the
//     order Mesh::boundaryPatches() itself returns
// combined via FNV-1a over each floating-point value's raw IEEE-754 bit
// pattern (not a text/decimal representation -- avoids any ambiguity
// from formatting choices) and each integer value's own bits.
// Deliberately excludes anything not intrinsic to the mesh's own
// definition: object addresses, container/hash-map iteration order
// (cells()/faces()/boundaryPatches() are all plain, insertion-ordered
// std::vectors, so this is naturally already satisfied), timestamps, and
// filesystem paths.
//
// Not a cryptographic hash -- collision resistance against a
// deliberately adversarial mesh is not the goal here (restart mesh-
// compatibility checking, not security); a 64-bit FNV-1a value is
// already astronomically unlikely to collide between two meshes that
// differ in any way a real case would produce.
//
// Portability note: hashes the platform's native IEEE-754 double byte
// representation directly, so a fingerprint computed on one machine is
// only guaranteed to match a fingerprint recomputed for the identical
// mesh on a machine with the same endianness -- not claimed or tested
// cross-architecture here.
[[nodiscard]] std::string computeMeshFingerprint(const Mesh& mesh);

}  // namespace cfd::mesh
