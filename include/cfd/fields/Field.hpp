#pragma once

#include <algorithm>
#include <vector>

namespace cfd::fields {

// Generic dense, contiguous storage for one value per mesh entity (cell or
// face) -- the semantic distinction (which entity, what it means) is added
// by ScalarField/VectorField/SurfaceField, not here. A Field never owns
// mesh topology and never contains solver logic; it is purely a sized
// container with elementwise-friendly access. The caller decides its size
// (e.g. mesh.numberOfCells()) -- Field itself does not depend on Mesh.
template <typename T>
class Field {
 public:
  using value_type = T;
  using container_type = std::vector<T>;
  using size_type = typename container_type::size_type;
  using iterator = typename container_type::iterator;
  using const_iterator = typename container_type::const_iterator;

  Field() = default;

  explicit Field(size_type size) : values_(size) {}
  Field(size_type size, const T& initialValue) : values_(size, initialValue) {}

  [[nodiscard]] size_type size() const noexcept { return values_.size(); }
  [[nodiscard]] bool empty() const noexcept { return values_.empty(); }

  // Fast, unchecked access -- use in hot loops once indices are trusted.
  T& operator[](size_type index) noexcept { return values_[index]; }
  [[nodiscard]] const T& operator[](size_type index) const noexcept { return values_[index]; }

  // Checked access -- throws std::out_of_range (via std::vector::at) for
  // an invalid index. Use in tests, parsing, and other non-hot-loop paths.
  // Never silently returns a default value for an invalid index.
  T& at(size_type index) { return values_.at(index); }
  [[nodiscard]] const T& at(size_type index) const { return values_.at(index); }

  // Contiguous access for numerical kernels, MPI, CUDA transfers,
  // serialization, etc. Does not transfer ownership -- the Field still
  // owns the data.
  T* data() noexcept { return values_.data(); }
  [[nodiscard]] const T* data() const noexcept { return values_.data(); }

  void fill(const T& value) { std::fill(values_.begin(), values_.end(), value); }

  iterator begin() noexcept { return values_.begin(); }
  iterator end() noexcept { return values_.end(); }
  [[nodiscard]] const_iterator begin() const noexcept { return values_.begin(); }
  [[nodiscard]] const_iterator end() const noexcept { return values_.end(); }
  [[nodiscard]] const_iterator cbegin() const noexcept { return values_.cbegin(); }
  [[nodiscard]] const_iterator cend() const noexcept { return values_.cend(); }

 private:
  container_type values_;
};

}  // namespace cfd::fields
