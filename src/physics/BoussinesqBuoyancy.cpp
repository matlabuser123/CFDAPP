#include "cfd/physics/BoussinesqBuoyancy.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::physics {

namespace {

void validatePositiveFinite(Real value, const char* fieldName) {
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw InvalidArgumentError(std::string("BoussinesqBuoyancy: ") + fieldName +
                               " must be finite and > 0");
  }
}

void validateNonNegativeFinite(Real value, const char* fieldName) {
  if (!std::isfinite(value) || !(value >= 0.0)) {
    throw InvalidArgumentError(std::string("BoussinesqBuoyancy: ") + fieldName +
                               " must be finite and >= 0");
  }
}

void validateFinite(Real value, const char* fieldName) {
  if (!std::isfinite(value)) {
    throw InvalidArgumentError(std::string("BoussinesqBuoyancy: ") + fieldName + " must be finite");
  }
}

}  // namespace

BoussinesqBuoyancy::BoussinesqBuoyancy(Real referenceDensity, Real beta, Real referenceTemperature,
                                       Vector2 gravity)
    : referenceDensity_(referenceDensity),
      beta_(beta),
      referenceTemperature_(referenceTemperature),
      gravity_(gravity) {
  validatePositiveFinite(referenceDensity_, "referenceDensity");
  validateNonNegativeFinite(beta_, "beta");
  validateFinite(referenceTemperature_, "referenceTemperature");
  validateFinite(gravity_.x, "gravity.x");
  validateFinite(gravity_.y, "gravity.y");
}

Real BoussinesqBuoyancy::referenceDensity() const noexcept { return referenceDensity_; }
Real BoussinesqBuoyancy::beta() const noexcept { return beta_; }
Real BoussinesqBuoyancy::referenceTemperature() const noexcept { return referenceTemperature_; }
Vector2 BoussinesqBuoyancy::gravity() const noexcept { return gravity_; }

Vector2 BoussinesqBuoyancy::source(Real temperature) const {
  validateFinite(temperature, "temperature");
  // See this class's own header comment for the full hydrostatic-
  // subtraction derivation of the leading minus sign -- not optional.
  return -(referenceDensity_ * beta_ * (temperature - referenceTemperature_)) * gravity_;
}

}  // namespace cfd::physics
