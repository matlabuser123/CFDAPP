import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// GUI-003 -- Physics Editor: edits cfd::io::PhysicsConfig via
// simulationController.setPhysicsConfig(). Only exposes modules that are
// genuinely production-integrated (thermal, turbulence, buoyancy,
// species, multiphase, compressible -- ProjectRunner.hpp's own header
// comment names exactly this set). "model" itself is not editable here:
// PhysicsConfigParser.cpp accepts exactly one value
// ("incompressible_laminar") for the top-level physics model today, so
// this page shows it as a fact, not a choice. Every add/remove/toggle
// below edits a local `draft` object (a plain JS copy of
// simulationController.physicsConfig) and commits the *whole* section
// through setPhysicsConfig() on each change -- draft is reset from the
// authoritative model on every caseChanged, so it never becomes a
// second, independently-diverging copy of the case (it is only ever a
// staging area for the in-progress edit, immediately reconciled).
// Cross-field rules (multiphase+turbulence mutual exclusion, buoyancy
// requiring thermal, etc.) are NOT re-checked here -- Apply && Validate
// calls the same validateDraft() -> CaseWriter/CaseReader/CaseBuilder
// round trip every other page uses, so this page cannot silently accept
// something production validation would reject.
ColumnLayout {
    id: root
    spacing: 10

    readonly property bool hasCase: Object.keys(simulationController.physicsConfig).length > 0
    property var draft: ({})

    function resetDraft() { draft = JSON.parse(JSON.stringify(simulationController.physicsConfig)) }
    function commit() { simulationController.setPhysicsConfig(draft); draft = draft }

    Connections {
        target: simulationController
        function onCaseChanged() { resetDraft() }
    }
    Component.onCompleted: resetDraft()

    Label { text: "No case is open. Use New Case or Open on the Case page first."; visible: !hasCase }

    Flickable {
        visible: hasCase
        Layout.fillWidth: true
        Layout.fillHeight: true
        contentHeight: content.implicitHeight
        clip: true

        ColumnLayout {
            id: content
            width: parent.width
            spacing: 10

            GroupBox {
                title: "Fluid"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Label { text: "Model: incompressible_laminar (the only model this build's case format supports)" }
                    RowLayout {
                        Label { text: "Density"; Layout.preferredWidth: 140 }
                        TextField {
                            text: draft.density !== undefined ? draft.density : ""
                            validator: DoubleValidator { bottom: 0 }
                            onEditingFinished: { draft.density = parseFloat(text) || 0; commit() }
                        }
                    }
                    RowLayout {
                        Label { text: "Dynamic viscosity"; Layout.preferredWidth: 140 }
                        TextField {
                            text: draft.dynamicViscosity !== undefined ? draft.dynamicViscosity : ""
                            validator: DoubleValidator { bottom: 0 }
                            onEditingFinished: { draft.dynamicViscosity = parseFloat(text) || 0; commit() }
                        }
                    }
                    RowLayout {
                        Label { text: "Reynolds number"; Layout.preferredWidth: 140 }
                        TextField {
                            text: draft.reynoldsNumber !== undefined ? draft.reynoldsNumber : ""
                            placeholderText: "(optional)"
                            validator: DoubleValidator { bottom: 0 }
                            onEditingFinished: { draft.reynoldsNumber = text.length > 0 ? (parseFloat(text) || 0) : undefined; commit() }
                        }
                    }
                }
            }

            GroupBox {
                title: "Thermal"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    CheckBox {
                        text: "Enabled"
                        checked: draft.thermal !== undefined
                        onToggled: {
                            if (checked) draft.thermal = { conductivity: 0.6, specificHeat: 4180, initialTemperature: 300 }
                            else delete draft.thermal
                            commit()
                        }
                    }
                    ColumnLayout {
                        visible: draft.thermal !== undefined
                        RowLayout {
                            Label { text: "Conductivity"; Layout.preferredWidth: 140 }
                            TextField {
                                text: draft.thermal ? draft.thermal.conductivity : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.thermal.conductivity = parseFloat(text) || 0; commit() }
                            }
                        }
                        RowLayout {
                            Label { text: "Specific heat"; Layout.preferredWidth: 140 }
                            TextField {
                                text: draft.thermal ? draft.thermal.specificHeat : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.thermal.specificHeat = parseFloat(text) || 0; commit() }
                            }
                        }
                        RowLayout {
                            Label { text: "Initial temperature"; Layout.preferredWidth: 140 }
                            TextField {
                                text: draft.thermal ? draft.thermal.initialTemperature : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.thermal.initialTemperature = parseFloat(text) || 0; commit() }
                            }
                        }
                    }
                }
            }

            GroupBox {
                title: "Turbulence"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    RowLayout {
                        Label { text: "Model"; Layout.preferredWidth: 140 }
                        ComboBox {
                            id: turbulenceModelBox
                            model: simulationController.turbulenceModelNames()
                            currentIndex: draft.turbulence ? model.indexOf(draft.turbulence.model) : 0
                            onActivated: {
                                var name = model[currentIndex]
                                if (name === "laminar") {
                                    delete draft.turbulence
                                } else {
                                    var t = draft.turbulence || { initialK: 0.01 }
                                    t.model = name
                                    if (name === "k_epsilon") { t.initialEpsilon = t.initialEpsilon || 0.01; delete t.initialOmega }
                                    else { t.initialOmega = t.initialOmega || 1.0; delete t.initialEpsilon }
                                    draft.turbulence = t
                                }
                                commit()
                            }
                        }
                    }
                    ColumnLayout {
                        visible: draft.turbulence !== undefined
                        RowLayout {
                            Label { text: "Initial k"; Layout.preferredWidth: 140 }
                            TextField {
                                text: draft.turbulence ? draft.turbulence.initialK : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.turbulence.initialK = parseFloat(text) || 0; commit() }
                            }
                        }
                        RowLayout {
                            visible: draft.turbulence && draft.turbulence.model === "k_epsilon"
                            Label { text: "Initial epsilon"; Layout.preferredWidth: 140 }
                            TextField {
                                text: draft.turbulence && draft.turbulence.initialEpsilon !== undefined ? draft.turbulence.initialEpsilon : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.turbulence.initialEpsilon = parseFloat(text) || 0; commit() }
                            }
                        }
                        RowLayout {
                            visible: draft.turbulence && (draft.turbulence.model === "k_omega" || draft.turbulence.model === "sst")
                            Label { text: "Initial omega"; Layout.preferredWidth: 140 }
                            TextField {
                                text: draft.turbulence && draft.turbulence.initialOmega !== undefined ? draft.turbulence.initialOmega : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.turbulence.initialOmega = parseFloat(text) || 0; commit() }
                            }
                        }
                    }
                }
            }

            GroupBox {
                title: "Buoyancy (requires Thermal enabled)"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    CheckBox {
                        text: "Enabled"
                        checked: draft.buoyancy !== undefined
                        onToggled: {
                            if (checked) draft.buoyancy = { beta: 0.001, referenceTemperature: 300, gravityX: 0, gravityY: -9.81 }
                            else delete draft.buoyancy
                            commit()
                        }
                    }
                    ColumnLayout {
                        visible: draft.buoyancy !== undefined
                        RowLayout {
                            Label { text: "Thermal expansion (beta)"; Layout.preferredWidth: 160 }
                            TextField {
                                text: draft.buoyancy ? draft.buoyancy.beta : ""
                                validator: DoubleValidator {}
                                onEditingFinished: { draft.buoyancy.beta = parseFloat(text) || 0; commit() }
                            }
                        }
                        RowLayout {
                            Label { text: "Reference temperature"; Layout.preferredWidth: 160 }
                            TextField {
                                text: draft.buoyancy ? draft.buoyancy.referenceTemperature : ""
                                validator: DoubleValidator {}
                                onEditingFinished: { draft.buoyancy.referenceTemperature = parseFloat(text) || 0; commit() }
                            }
                        }
                        RowLayout {
                            Label { text: "Gravity X / Y"; Layout.preferredWidth: 160 }
                            TextField {
                                Layout.preferredWidth: 100
                                text: draft.buoyancy ? draft.buoyancy.gravityX : ""
                                validator: DoubleValidator {}
                                onEditingFinished: { draft.buoyancy.gravityX = parseFloat(text) || 0; commit() }
                            }
                            TextField {
                                Layout.preferredWidth: 100
                                text: draft.buoyancy ? draft.buoyancy.gravityY : ""
                                validator: DoubleValidator {}
                                onEditingFinished: { draft.buoyancy.gravityY = parseFloat(text) || 0; commit() }
                            }
                        }
                    }
                }
            }

            GroupBox {
                title: "Species"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Repeater {
                        model: draft.species || []
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                Layout.preferredWidth: 100
                                text: modelData.name
                                onEditingFinished: { draft.species[index].name = text; commit() }
                            }
                            Label { text: "diffusivity" }
                            TextField {
                                Layout.preferredWidth: 90
                                text: modelData.diffusivity
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.species[index].diffusivity = parseFloat(text) || 0; commit() }
                            }
                            Label { text: "initial conc." }
                            TextField {
                                Layout.preferredWidth: 90
                                text: modelData.initialConcentration
                                validator: DoubleValidator {}
                                onEditingFinished: { draft.species[index].initialConcentration = parseFloat(text) || 0; commit() }
                            }
                            Button {
                                text: "Remove"
                                onClicked: { draft.species.splice(index, 1); commit() }
                            }
                        }
                    }
                    Button {
                        text: "Add species"
                        onClicked: {
                            if (!draft.species) draft.species = []
                            draft.species.push({ name: "species" + (draft.species.length + 1), diffusivity: 1e-5, initialConcentration: 0 })
                            commit()
                        }
                    }
                }
            }

            GroupBox {
                title: "Multiphase (mutually exclusive with Turbulence)"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    CheckBox {
                        text: "Enabled"
                        checked: draft.multiphase !== undefined
                        onToggled: {
                            if (checked) {
                                draft.multiphase = {
                                    phase1: { name: "phase1", density: 1000, viscosity: 1e-3 },
                                    phase2: { name: "phase2", density: 1, viscosity: 1.8e-5 },
                                    initialAlpha: 0.5, transportTimeStep: 0.01
                                }
                            } else delete draft.multiphase
                            commit()
                        }
                    }
                    ColumnLayout {
                        visible: draft.multiphase !== undefined
                        RowLayout {
                            Label { text: "Phase 1: name/density/viscosity"; Layout.preferredWidth: 200 }
                            TextField { Layout.preferredWidth: 90; text: draft.multiphase ? draft.multiphase.phase1.name : ""
                                onEditingFinished: { draft.multiphase.phase1.name = text; commit() } }
                            TextField { Layout.preferredWidth: 90; text: draft.multiphase ? draft.multiphase.phase1.density : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.multiphase.phase1.density = parseFloat(text) || 0; commit() } }
                            TextField { Layout.preferredWidth: 90; text: draft.multiphase ? draft.multiphase.phase1.viscosity : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.multiphase.phase1.viscosity = parseFloat(text) || 0; commit() } }
                        }
                        RowLayout {
                            Label { text: "Phase 2: name/density/viscosity"; Layout.preferredWidth: 200 }
                            TextField { Layout.preferredWidth: 90; text: draft.multiphase ? draft.multiphase.phase2.name : ""
                                onEditingFinished: { draft.multiphase.phase2.name = text; commit() } }
                            TextField { Layout.preferredWidth: 90; text: draft.multiphase ? draft.multiphase.phase2.density : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.multiphase.phase2.density = parseFloat(text) || 0; commit() } }
                            TextField { Layout.preferredWidth: 90; text: draft.multiphase ? draft.multiphase.phase2.viscosity : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.multiphase.phase2.viscosity = parseFloat(text) || 0; commit() } }
                        }
                        RowLayout {
                            Label { text: "Initial alpha (0-1)"; Layout.preferredWidth: 200 }
                            TextField {
                                text: draft.multiphase ? draft.multiphase.initialAlpha : ""
                                validator: DoubleValidator { bottom: 0; top: 1 }
                                onEditingFinished: { draft.multiphase.initialAlpha = parseFloat(text) || 0; commit() }
                            }
                        }
                        RowLayout {
                            Label { text: "Transport time step"; Layout.preferredWidth: 200 }
                            TextField {
                                text: draft.multiphase ? draft.multiphase.transportTimeStep : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.multiphase.transportTimeStep = parseFloat(text) || 0; commit() }
                            }
                        }
                    }
                }
            }

            GroupBox {
                title: "Compressible (post-hoc evaluation, not a coupled compressible solve)"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                        text: "This foundation reinterprets the converged incompressible result (density/Mach/continuity) after the fact -- it does not solve a coupled compressible flow."
                        font.italic: true
                    }
                    CheckBox {
                        text: "Enabled"
                        checked: draft.compressible !== undefined
                        onToggled: {
                            if (checked) draft.compressible = { gasConstant: 287, specificHeatPressure: 1005, referencePressure: 101325, thermalCoupled: false, temperature: 300 }
                            else delete draft.compressible
                            commit()
                        }
                    }
                    ColumnLayout {
                        visible: draft.compressible !== undefined
                        RowLayout {
                            Label { text: "Gas constant"; Layout.preferredWidth: 160 }
                            TextField {
                                text: draft.compressible ? draft.compressible.gasConstant : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.compressible.gasConstant = parseFloat(text) || 0; commit() }
                            }
                        }
                        RowLayout {
                            Label { text: "Cp (specific heat, const P)"; Layout.preferredWidth: 160 }
                            TextField {
                                text: draft.compressible ? draft.compressible.specificHeatPressure : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.compressible.specificHeatPressure = parseFloat(text) || 0; commit() }
                            }
                        }
                        RowLayout {
                            Label { text: "Reference pressure"; Layout.preferredWidth: 160 }
                            TextField {
                                text: draft.compressible ? draft.compressible.referencePressure : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.compressible.referencePressure = parseFloat(text) || 0; commit() }
                            }
                        }
                        CheckBox {
                            text: "Thermal-coupled (use the case's own converged temperature field instead of a fixed value; requires Thermal enabled)"
                            checked: draft.compressible ? draft.compressible.thermalCoupled : false
                            onToggled: { draft.compressible.thermalCoupled = checked; commit() }
                        }
                        RowLayout {
                            visible: draft.compressible && !draft.compressible.thermalCoupled
                            Label { text: "Fixed temperature"; Layout.preferredWidth: 160 }
                            TextField {
                                text: draft.compressible && draft.compressible.temperature !== undefined ? draft.compressible.temperature : ""
                                validator: DoubleValidator { bottom: 0 }
                                onEditingFinished: { draft.compressible.temperature = parseFloat(text) || 0; commit() }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Button { text: "Apply && Validate"; onClicked: simulationController.validateDraft() }
            }
            ValidationPanel { Layout.fillWidth: true }
        }
    }
}
