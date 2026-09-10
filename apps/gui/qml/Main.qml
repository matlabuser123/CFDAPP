import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// GUI-006 -- navigation shell: Case / Mesh / Physics / Boundaries /
// Solver / Results, one authoritative case model
// (simulationController -> cfd::app::CaseSession, see
// SimulationController.hpp's own "P7-GUI -- Case Editing" header
// comment) shared by every page below -- no page owns its own copy of
// mesh/physics/BC/solver values, each one just re-reads
// simulationController.meshConfig/physicsConfig/boundaryConfig/
// solverConfig, which are always the live in-memory CaseDefinition.
// currentPage/requestNewCase()/requestOpenCase() live here (not on each
// page) so every page can reach them the same way, via the
// QtQuick.Window "Window.window" attached property (each page is a
// separate top-level QML component, so a plain `id` reference from Main
// is not visible inside them -- Window.window is the standard QML idiom
// for "reach the root window from any descendant").
ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 820
    title: simulationController.applicationName + " " + simulationController.applicationVersion +
           (simulationController.caseName.length > 0 ? " — " + simulationController.caseName : "") +
           (simulationController.isModified ? " *" : "")

    property string currentPage: "case"
    property string pendingAction: ""
    property string pendingOpenPath: ""

    readonly property var pageNames: ["case", "mesh", "physics", "boundaries", "solver", "results"]
    readonly property var pageLabels: ["Case", "Mesh", "Physics", "Boundaries", "Solver", "Results"]

    // GUI-008 -- dirty-state guard: New Case / Open Case never silently
    // discard an unsaved edit (simulationController.isModified mirrors
    // CaseSession's own Modified state -- see CaseSession.hpp's own state
    // machine comment).
    function requestNewCase() {
        if (simulationController.isModified) {
            pendingAction = "new"
            unsavedChangesDialog.open()
        } else {
            simulationController.newCase()
        }
    }
    function requestOpenCase(path) {
        if (simulationController.isModified) {
            pendingAction = "open"
            pendingOpenPath = path
            unsavedChangesDialog.open()
        } else {
            simulationController.openCase(path)
        }
    }
    function continuePendingAction() {
        if (pendingAction === "new") simulationController.newCase()
        else if (pendingAction === "open") simulationController.openCase(pendingOpenPath)
        pendingAction = ""
    }

    Dialog {
        id: unsavedChangesDialog
        title: "Unsaved changes"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Cancel
        ColumnLayout {
            Label { text: "This case has unsaved changes. Save before continuing?" }
            RowLayout {
                Button {
                    text: "Save"
                    onClicked: {
                        var ok = simulationController.caseDirectory.length > 0 && simulationController.save()
                        unsavedChangesDialog.close()
                        if (ok) continuePendingAction()
                    }
                }
                Button {
                    text: "Discard"
                    onClicked: { unsavedChangesDialog.close(); continuePendingAction() }
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        ColumnLayout {
            Layout.preferredWidth: 130
            Layout.fillHeight: true
            spacing: 4

            Repeater {
                model: pageLabels
                delegate: Button {
                    Layout.fillWidth: true
                    text: modelData
                    highlighted: currentPage === pageNames[index]
                    onClicked: currentPage = pageNames[index]
                }
            }

            Item { Layout.fillHeight: true }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                font.pixelSize: 11
                color: simulationController.validationStatus.valid === false ? "#b00020" : "#2e7d32"
                text: simulationController.validationStatus.valid === false ? "Validation: FAILED" : "Validation: OK"
            }

            Button { text: "About"; Layout.fillWidth: true; onClicked: aboutDialog.open() }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: pageNames.indexOf(currentPage)

            CasePage { }
            MeshEditor { }
            PhysicsEditor { }
            BoundaryEditor { }
            SolverEditor { }
            ResultsPage { }
        }
    }

    Dialog {
        id: aboutDialog
        title: "About"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok

        ColumnLayout {
            Label { text: simulationController.applicationName; font.bold: true }
            Label { text: "Version " + simulationController.applicationVersion }
            Label { text: "A 2D incompressible/thermal/turbulent CFD solver." }
        }
    }
}
