import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// GUI-005 -- Solver-Settings Editor: edits cfd::io::SolverConfig via
// simulationController.setSolverConfig(). Only exposes what SolverConfig
// (include/cfd/io/case/SolverConfig.hpp) and the production case-file
// pipeline (ProjectRunner -- steady SIMPLE only, see its own header
// comment) actually consume today: SIMPLE's own outer-iteration budget,
// tolerances, relaxation factors, and the two linear-solver specs
// (momentum/pressure). PISO/transient timestep/end-time/CFL/restart
// settings are deliberately NOT exposed here: they are real, tested
// library capabilities (TransientSolver, PISO -- see TODO.md P2), but
// ProjectRunner.hpp's own header comment documents this codebase's
// production case format as "steady-incompressible-SIMPLE(+thermal+
// species+multiphase+compressible) scope, exactly matching what
// CaseReader/CaseBuilder support today" -- there is no case.json/
// solver.json field for them, and CaseBuilder never dispatches to PISO/
// TransientSolver at all. Building a control for a setting the
// production pipeline would silently ignore would be worse than no
// control (an invisible no-op), so this page shows that gap explicitly
// instead of inventing a GUI-only setting with no effect.
ColumnLayout {
    id: root
    spacing: 10

    readonly property bool hasCase: Object.keys(simulationController.solverConfig).length > 0
    property var draft: ({})

    function resetDraft() { draft = JSON.parse(JSON.stringify(simulationController.solverConfig)) }
    function commit() { simulationController.setSolverConfig(draft); draft = draft }

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
                title: "Steady solver (SIMPLE)"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Label { text: "Type: " + (draft.type || "SIMPLE") + " (the only case-file-driven pressure-velocity solver this build supports)" }
                    RowLayout {
                        Label { text: "Max outer iterations"; Layout.preferredWidth: 180 }
                        TextField {
                            text: draft.maxIterations !== undefined ? draft.maxIterations : ""
                            validator: IntValidator { bottom: 1 }
                            onEditingFinished: { draft.maxIterations = parseInt(text) || 0; commit() }
                        }
                    }
                    RowLayout {
                        Label { text: "Velocity relaxation"; Layout.preferredWidth: 180 }
                        TextField {
                            text: draft.velocityRelaxation !== undefined ? draft.velocityRelaxation : ""
                            validator: DoubleValidator { bottom: 0; top: 1 }
                            onEditingFinished: { draft.velocityRelaxation = parseFloat(text) || 0; commit() }
                        }
                    }
                    RowLayout {
                        Label { text: "Pressure relaxation"; Layout.preferredWidth: 180 }
                        TextField {
                            text: draft.pressureRelaxation !== undefined ? draft.pressureRelaxation : ""
                            validator: DoubleValidator { bottom: 0; top: 1 }
                            onEditingFinished: { draft.pressureRelaxation = parseFloat(text) || 0; commit() }
                        }
                    }
                    RowLayout {
                        Label { text: "Velocity tolerance"; Layout.preferredWidth: 180 }
                        TextField {
                            text: draft.velocityTolerance !== undefined ? draft.velocityTolerance : ""
                            validator: DoubleValidator { bottom: 0 }
                            onEditingFinished: { draft.velocityTolerance = parseFloat(text) || 0; commit() }
                        }
                    }
                    RowLayout {
                        Label { text: "Pressure tolerance"; Layout.preferredWidth: 180 }
                        TextField {
                            text: draft.pressureTolerance !== undefined ? draft.pressureTolerance : ""
                            validator: DoubleValidator { bottom: 0 }
                            onEditingFinished: { draft.pressureTolerance = parseFloat(text) || 0; commit() }
                        }
                    }
                    RowLayout {
                        Label { text: "Continuity tolerance"; Layout.preferredWidth: 180 }
                        TextField {
                            text: draft.continuityTolerance !== undefined ? draft.continuityTolerance : ""
                            validator: DoubleValidator { bottom: 0 }
                            onEditingFinished: { draft.continuityTolerance = parseFloat(text) || 0; commit() }
                        }
                    }
                }
            }

            GroupBox {
                title: "Momentum linear solver"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Label { text: "Type: " + (draft.momentumSolver ? draft.momentumSolver.type : "BiCGSTAB") }
                    RowLayout {
                        Label { text: "Absolute tolerance"; Layout.preferredWidth: 160 }
                        TextField {
                            text: draft.momentumSolver ? draft.momentumSolver.absoluteTolerance : ""
                            validator: DoubleValidator { bottom: 0 }
                            onEditingFinished: { draft.momentumSolver.absoluteTolerance = parseFloat(text) || 0; commit() }
                        }
                    }
                    RowLayout {
                        Label { text: "Relative tolerance"; Layout.preferredWidth: 160 }
                        TextField {
                            text: draft.momentumSolver ? draft.momentumSolver.relativeTolerance : ""
                            validator: DoubleValidator { bottom: 0 }
                            onEditingFinished: { draft.momentumSolver.relativeTolerance = parseFloat(text) || 0; commit() }
                        }
                    }
                    RowLayout {
                        Label { text: "Max iterations"; Layout.preferredWidth: 160 }
                        TextField {
                            text: draft.momentumSolver ? draft.momentumSolver.maxIterations : ""
                            validator: IntValidator { bottom: 1 }
                            onEditingFinished: { draft.momentumSolver.maxIterations = parseInt(text) || 0; commit() }
                        }
                    }
                }
            }

            GroupBox {
                title: "Pressure linear solver"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Label { text: "Type: " + (draft.pressureSolver ? draft.pressureSolver.type : "BiCGSTAB") }
                    RowLayout {
                        Label { text: "Absolute tolerance"; Layout.preferredWidth: 160 }
                        TextField {
                            text: draft.pressureSolver ? draft.pressureSolver.absoluteTolerance : ""
                            validator: DoubleValidator { bottom: 0 }
                            onEditingFinished: { draft.pressureSolver.absoluteTolerance = parseFloat(text) || 0; commit() }
                        }
                    }
                    RowLayout {
                        Label { text: "Relative tolerance"; Layout.preferredWidth: 160 }
                        TextField {
                            text: draft.pressureSolver ? draft.pressureSolver.relativeTolerance : ""
                            validator: DoubleValidator { bottom: 0 }
                            onEditingFinished: { draft.pressureSolver.relativeTolerance = parseFloat(text) || 0; commit() }
                        }
                    }
                    RowLayout {
                        Label { text: "Max iterations"; Layout.preferredWidth: 160 }
                        TextField {
                            text: draft.pressureSolver ? draft.pressureSolver.maxIterations : ""
                            validator: IntValidator { bottom: 1 }
                            onEditingFinished: { draft.pressureSolver.maxIterations = parseInt(text) || 0; commit() }
                        }
                    }
                }
            }

            GroupBox {
                title: "Transient / PISO / restart"
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                    font.italic: true
                    text: "Not available through case files yet: PISO, timestep, end time, CFL " +
                          "monitoring, and restart are real, tested library capabilities, but the " +
                          "production case format and ProjectRunner (the only execution path this " +
                          "GUI or the CLI use) do not read or dispatch to them today -- see " +
                          "ProjectRunner.hpp's own documented scope. Exposing controls for them here " +
                          "would silently do nothing when a case runs, which is worse than not " +
                          "offering them; wiring them in is tracked separately."
                }
            }

            RowLayout {
                Button { text: "Apply && Validate"; onClicked: simulationController.validateDraft() }
            }
            ValidationPanel { Layout.fillWidth: true }
        }
    }
}
