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

namespace cfd::gui {

[[nodiscard]] QVariantMap toVariant(const cfd::io::CaseConfig& config);
[[nodiscard]] QVariantMap toVariant(const cfd::io::GeometryConfig& config);
[[nodiscard]] QVariantMap toVariant(const cfd::io::MeshConfig& config);
[[nodiscard]] QVariantMap toVariant(const cfd::io::PhysicsConfig& config);
[[nodiscard]] QVariantMap toVariant(const cfd::io::BoundaryConfig& config);
[[nodiscard]] QVariantMap toVariant(const cfd::io::SolverConfig& config);
[[nodiscard]] QVariantMap toVariant(const cfd::io::InitialConditions& config);

// caseConfigFromVariant only reads "name"/"description" (formatVersion is
// never GUI-editable -- see CaseConfig.hpp's own "the only currently-
// supported value is 1" comment; a fresh/edited case keeps whatever
// formatVersion it already had, defaulting to 1 via newCase()).
[[nodiscard]] cfd::io::CaseConfig caseConfigFromVariant(const QVariantMap& variant,
                                                        const cfd::io::CaseConfig& previous);
[[nodiscard]] cfd::io::GeometryConfig geometryConfigFromVariant(const QVariantMap& variant);
[[nodiscard]] cfd::io::MeshConfig meshConfigFromVariant(const QVariantMap& variant);
[[nodiscard]] cfd::io::PhysicsConfig physicsConfigFromVariant(const QVariantMap& variant);
[[nodiscard]] cfd::io::BoundaryConfig boundaryConfigFromVariant(const QVariantMap& variant);
[[nodiscard]] cfd::io::SolverConfig solverConfigFromVariant(const QVariantMap& variant);
[[nodiscard]] cfd::io::InitialConditions initialConditionsFromVariant(const QVariantMap& variant);

}  // namespace cfd::gui
