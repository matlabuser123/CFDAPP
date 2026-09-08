#include "cfd/mesh/MeshFingerprint.hpp"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "cfd/core/Vector2.hpp"

namespace cfd::mesh {

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void hashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<std::uint64_t>(bytes[i]);
    hash *= kFnvPrime;
  }
}

void hashReal(std::uint64_t& hash, Real value) {
  std::uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(Real), "Real must be a 64-bit IEEE-754 double");
  std::memcpy(&bits, &value, sizeof(bits));
  hashBytes(hash, &bits, sizeof(bits));
}

void hashVector2(std::uint64_t& hash, const Vector2& v) {
  hashReal(hash, v.x);
  hashReal(hash, v.y);
}

void hashIndex(std::uint64_t& hash, Index value) {
  const auto bits = static_cast<std::uint64_t>(value);
  hashBytes(hash, &bits, sizeof(bits));
}

void hashString(std::uint64_t& hash, const std::string& s) { hashBytes(hash, s.data(), s.size()); }

// Distinguishable from any real face id -- a boundary face's absent
// neighbor must hash differently than any internal face's actual
// neighbor id could.
constexpr std::uint64_t kNoNeighborSentinel = 0xFFFFFFFFFFFFFFFFULL;

}  // namespace

std::string computeMeshFingerprint(const Mesh& mesh) {
  std::uint64_t hash = kFnvOffsetBasis;

  for (const auto& cell : mesh.cells()) {
    hashVector2(hash, cell.centroid());
    hashReal(hash, cell.volume());
  }

  for (const auto& face : mesh.faces()) {
    hashIndex(hash, face.owner());
    if (face.neighbor().has_value()) {
      hashIndex(hash, *face.neighbor());
    } else {
      hashBytes(hash, &kNoNeighborSentinel, sizeof(kNoNeighborSentinel));
    }
    hashVector2(hash, face.centroid());
    hashVector2(hash, face.areaVector());
  }

  for (const auto& patch : mesh.boundaryPatches()) {
    hashString(hash, patch.name());
    for (const Index faceId : patch.faceIds()) {
      hashIndex(hash, faceId);
    }
  }

  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}

}  // namespace cfd::mesh
