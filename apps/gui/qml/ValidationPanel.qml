import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

// GUI-007: the one central validation display every page shares --
// renders simulationController.validationIssues (populated by
// validateDraft()'s real CaseWriter/CaseReader/CaseBuilder round trip,
// see SimulationControllerEditing.cpp's own header comment). Always 0 or
// 1 entries today (CaseReader/CaseBuilder fail fast on the first
// problem, same as the CLI) -- this Repeater-over-a-list shape is still
// the right one to build against, since it renders correctly whether the
// list holds zero, one, or (if the underlying validation is ever
// extended to collect more than one issue) several entries, with no QML
// change required either way. Clicking an issue switches to its section
// (Mesh/Physics/Boundaries/Solver/Case) -- "clicking an error should
// switch to the corresponding editor".
ColumnLayout {
    id: root
    spacing: 4

    Label {
        text: simulationController.validationIssues.length === 0
              ? (simulationController.validationStatus.valid === false ? "" : "No validation errors.")
              : ""
        color: "#2e7d32"
        visible: text.length > 0
    }

    Repeater {
        model: simulationController.validationIssues
        delegate: Rectangle {
            Layout.fillWidth: true
            implicitHeight: issueLabel.implicitHeight + 12
            color: "#fdecea"
            border.color: "#b00020"
            radius: 3

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    var section = (modelData.section || "").toLowerCase()
                    if (["mesh", "physics", "boundaries", "solver"].indexOf(section) !== -1) {
                        Window.window.currentPage = section
                    } else {
                        Window.window.currentPage = "case"
                    }
                }
            }

            Label {
                id: issueLabel
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 6
                wrapMode: Text.WrapAnywhere
                color: "#611a15"
                text: modelData.severity + " | " + modelData.section +
                      (modelData.field ? (" | " + modelData.field) : "") +
                      " | " + modelData.message
            }
        }
    }
}
