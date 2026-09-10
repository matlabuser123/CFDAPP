#include "cfd/io/CSVWriter.hpp"

#include <cmath>

#include "DeterministicOstream.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::io {

using cfd::Index;
using cfd::Real;
using cfd::mesh::Mesh;
using cfd::pressure_velocity::SIMPLEResult;

void CSVWriter::writeFields(const std::filesystem::path& path, const Mesh& mesh,
                            const SIMPLEResult& result,
                            const std::optional<cfd::fields::ScalarField>& temperature,
                            const std::vector<NamedScalarField>& species) {
  const Index n = mesh.numberOfCells();
  if (result.velocity.size() != n || result.pressure.size() != n) {
    throw InvalidArgumentError(
        "CSVWriter::writeFields: result field size does not match mesh cell count");
  }
  if (temperature.has_value() && temperature->size() != n) {
    throw InvalidArgumentError(
        "CSVWriter::writeFields: temperature size does not match mesh cell count");
  }
  for (const auto& [name, field] : species) {
    if (field.size() != n) {
      throw InvalidArgumentError("CSVWriter::writeFields: species \"" + name +
                                 "\" field size does not match mesh cell count");
    }
  }

  auto out = detail::openDeterministicOutput(path);
  out << "cell_id,x,y,velocity_x,velocity_y,velocity_magnitude,pressure";
  if (temperature.has_value()) out << ",temperature";
  for (const auto& [name, unused] : species) {
    (void)unused;
    out << ",concentration_" << name;
  }
  out << '\n';
  for (Index id = 0; id < n; ++id) {
    const Vector2& centroid = mesh.cell(id).centroid();
    const Vector2& velocity = result.velocity[id];
    const Real pressure = result.pressure[id];
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(pressure)) {
      throw NumericalError("CSVWriter::writeFields: non-finite value at cell " +
                           std::to_string(id));
    }
    if (temperature.has_value() && !std::isfinite((*temperature)[id])) {
      throw NumericalError("CSVWriter::writeFields: non-finite temperature at cell " +
                           std::to_string(id));
    }
    for (const auto& [name, field] : species) {
      if (!std::isfinite(field[id])) {
        throw NumericalError("CSVWriter::writeFields: non-finite species \"" + name +
                             "\" value at cell " + std::to_string(id));
      }
    }
    out << id << ',' << centroid.x << ',' << centroid.y << ',' << velocity.x << ',' << velocity.y
        << ',' << magnitude(velocity) << ',' << pressure;
    if (temperature.has_value()) out << ',' << (*temperature)[id];
    for (const auto& [name, field] : species) {
      (void)name;
      out << ',' << field[id];
    }
    out << '\n';
  }
}

void CSVWriter::writeResiduals(const std::filesystem::path& path, const SIMPLEResult& result) {
  const std::size_t n = result.uResidualHistory.size();
  if (result.vResidualHistory.size() != n || result.pressureResidualHistory.size() != n ||
      result.continuityHistory.size() != n) {
    throw InvalidArgumentError(
        "CSVWriter::writeResiduals: residual history arrays have inconsistent lengths");
  }

  auto out = detail::openDeterministicOutput(path);
  out << "iteration,u_residual,v_residual,p_residual,continuity_residual,global_mass_imbalance\n";
  // globalMassImbalance is SIMPLEResult's single *final* value (see
  // SIMPLEResult.hpp) -- SIMPLE does not track a per-iteration history of
  // it the way it does for the four residuals, so the same value is
  // repeated on every row rather than fabricating a history that was
  // never computed.
  for (std::size_t k = 0; k < n; ++k) {
    out << (k + 1) << ',' << result.uResidualHistory[k] << ',' << result.vResidualHistory[k] << ','
        << result.pressureResidualHistory[k] << ',' << result.continuityHistory[k] << ','
        << result.globalMassImbalance << '\n';
  }
}

}  // namespace cfd::io
