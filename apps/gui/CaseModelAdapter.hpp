#pragma once

// P7-GUI -- GUI Case Editing: the one QML<->C++ marshalling layer for
// cfd::io::CaseDefinition's six sections (mesh/geometry/physics/
// boundaries/solver/initial conditions) plus case.json's own name/
// description. Every function here is pure data conversion, never
// validation -- "*ToVariant" builds the QVariantMap a QML editor page
// binds its controls to; "*FromVariant" reads a QML editor's edited
// QVariantMap back into the exact same typed cfd::io::* struct
// CaseReader/CaseBuilder/CaseWriter already use, applying a structural
// default (0/""/empty) for anything missing or the wrong QVariant type,
// never rejecting a value as physically invalid -- physical validity
// (density > 0, a BC type CaseBuilder actually supports, etc.) is
// decided in exactly one place, the real parse+build pipeline
// (SimulationController::validateDraft(), see its own header comment),
// never re-decided here. This keeps "the GUI must not have a separate
// weaker validation path" true by construction: this layer cannot reject
// anything, so it cannot silently accept something the real pipeline
// would not.
//
// QVariantMap shapes mirror physics.json/boundaries.json/etc.'s own JSON
// shape closely (snake_case JSON keys become camelCase QVariantMap keys,
// e.g. "dynamic_viscosity" -> "dynamicViscosity") -- an optional block
// (thermal/turbulence/buoyancy/multiphase/compressible, and a per-patch
// temperature/alpha) is represented by the key being *absent* from the
// map when off (QML: `physics.thermal !== undefined`), matching
// PhysicsConfig.hpp's own "presence is the enable flag" convention
// exactly rather than inventing a second null-vs-absent distinction.

#include <QVariantList>
#include <QVariantMap>

#include "cfd/io/case/CaseDefinition.hpp"
#include "cfd/mesh/MeshQuality.hpp"

namespace cfd::gui {

[[nodiscard]] QVariantMap toVariant(const cfd::io::CaseConfig& config);
[[nodiscard]] QVariantMap toVariant(const cfd::io::GeometryConfig& config);
[[nodiscard]] QVariantMap toVariant(const cfd::io::MeshConfig& config);
[[nodiscard]] QVariantMap toVariant(const cfd::io::PhysicsConfig& config);
[[nodiscard]] QVariantMap toVariant(const cfd::io::BoundaryConfig& config);
[[nodiscard]] QVariantMap toVariant(const cfd::io::SolverConfig& config);
[[nodiscard]] QVariantMap toVariant(const cfd::io::InitialConditions& config);
// P12-MESH-004: the production mesh-quality report, read-only, for the GUI
// -- {status, summary, cells, minimumCellArea, maximumCellArea,
// maximumAspectRatio, maximumNonOrthogonality, maximumSkewness,
// maximumExpansionRatio, degenerateCells, invalidFaces, issues: [{severity,
// metric, message, text}]} (text = formatMeshQualityIssue).
[[nodiscard]] QVariantMap toVariant(const cfd::mesh::MeshQualityReport& report);

// caseConfigFromVariant only reads "name"/"description" (formatVersion is
// never GUI-editable -- see CaseConfig.hpp's own "the only currently-
// supported value is 1" comment; a fresh/edited case keeps whatever
// formatVersion it already had, defaulting to 1 via newCase()).
[[nodiscard]] cfd::io::CaseConfig caseConfigFromVariant(const QVariantMap& variant,
                                                        const cfd::io::CaseConfig& previous);
// P12-MESH-006: `previous` supplies what an editor map may omit -- a box geometry's depth, a 3D
// mesh's nz, the initial w -- so a 3D case keeps them through a commit from a page that does not
// show them. Velocity maps carry valueZ / velocityZ (0 in 2D).
[[nodiscard]] cfd::io::GeometryConfig geometryConfigFromVariant(
    const QVariantMap& variant, const cfd::io::GeometryConfig& previous = {});
[[nodiscard]] cfd::io::MeshConfig meshConfigFromVariant(const QVariantMap& variant,
                                                        const cfd::io::MeshConfig& previous = {});
[[nodiscard]] cfd::io::PhysicsConfig physicsConfigFromVariant(const QVariantMap& variant);
[[nodiscard]] cfd::io::BoundaryConfig boundaryConfigFromVariant(const QVariantMap& variant);
[[nodiscard]] cfd::io::SolverConfig solverConfigFromVariant(const QVariantMap& variant);
[[nodiscard]] cfd::io::InitialConditions initialConditionsFromVariant(
    const QVariantMap& variant, const cfd::io::InitialConditions& previous = {});

}  // namespace cfd::gui
