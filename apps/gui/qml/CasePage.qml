import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

// GUI-006/GUI-007: case-level actions (New/Open/Save/Save As/Validate/
// Run/Stop), case metadata editing (name/description, via
// simulationController.setCaseMetadata()), and the central validation
// panel (simulationController.validationIssues). All actions here call
// straight through to simulationController -> CaseSession ->
// ProjectRunner -- this page holds no case state of its own beyond the
// text field a user is actively typing into.
ColumnLayout {
    id: root
    spacing: 10

    GroupBox {
        title: "Case"
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Directory:" }
                TextField {
                    id: caseDirectoryField
                    Layout.fillWidth: true
                    placeholderText: "Case directory"
                    text: simulationController.caseDirectory
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Name:" }
                TextField {
                    id: caseNameField
                    Layout.fillWidth: true
                    text: simulationController.caseMetadata.name || ""
                    enabled: simulationController.canSave
                    onEditingFinished: {
                        var meta = simulationController.caseMetadata
                        meta.name = text
                        simulationController.setCaseMetadata(meta)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Description:" }
                TextField {
                    id: caseDescriptionField
                    Layout.fillWidth: true
                    text: simulationController.caseMetadata.description || ""
                    enabled: simulationController.canSave
                    onEditingFinished: {
                        var meta = simulationController.caseMetadata
                        meta.description = text
                        simulationController.setCaseMetadata(meta)
                    }
                }
            }

            RowLayout {
                Button { text: "New Case"; onClicked: Window.window.requestNewCase() }
                Button { text: "Open"; onClicked: Window.window.requestOpenCase(caseDirectoryField.text) }
                Button {
                    text: "Save"
                    enabled: simulationController.canSave
                    onClicked: {
                        if (simulationController.caseDirectory.length > 0) {
                            simulationController.save()
                        } else {
                            saveAsDialog.currentPath = caseDirectoryField.text
                            saveAsDialog.open()
                        }
                    }
                }
                Button {
                    text: "Save As..."
                    enabled: simulationController.canSave
                    onClicked: { saveAsDialog.currentPath = caseDirectoryField.text; saveAsDialog.open() }
                }
            }

            Label {
                text: "State: " + simulationController.stateName + (simulationController.isModified ? "  (unsaved changes)" : "")
                color: simulationController.isModified ? "#b06000" : "#404040"
            }
        }
    }

    Dialog {
        id: saveAsDialog
        title: "Save As"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        property alias currentPath: saveAsField.text
        onAccepted: simulationController.saveAs(saveAsField.text)
        ColumnLayout {
            Label { text: "Destination directory (created if it does not exist):" }
            TextField { id: saveAsField; Layout.preferredWidth: 360 }
        }
    }

    GroupBox {
        title: "Validate & Run"
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            spacing: 6
            RowLayout {
                Button { text: "Validate"; enabled: simulationController.canRun; onClicked: simulationController.validateDraft() }
                Button { text: "Run"; enabled: simulationController.canRun; onClicked: simulationController.run() }
                Button { text: "Stop"; enabled: simulationController.canStop; onClicked: simulationController.stop() }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Iteration " + simulationController.iteration + " / " + simulationController.maxIterations }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0; to: Math.max(simulationController.maxIterations, 1)
                    value: simulationController.iteration
                }
            }
            Label { text: "Continuity residual: " + simulationController.continuityResidual.toExponential(3) }

            Label {
                Layout.fillWidth: true
                color: "#b00020"
                wrapMode: Text.WrapAnywhere
                text: simulationController.lastError
                visible: simulationController.lastError.length > 0
            }
        }
    }

    ValidationPanel { Layout.fillWidth: true }

    Item { Layout.fillHeight: true }
}
