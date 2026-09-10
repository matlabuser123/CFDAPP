import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// GUI-002 -- Mesh Editor: edits cfd::io::MeshConfig + GeometryConfig
// (the only two sections a structured-Cartesian case's grid spans) via
// simulationController.setMeshAndGeometry() -- never a QML-local copy of
// these values (meshConfig()/geometryConfig() re-read straight from the
// authoritative CaseSession after every commit, the same "one model, not
// one copy per page" convention every editor page here follows). No
// solver/meshing logic lives here: nx*ny/dx/dy come from
// simulationController.meshCellInfo() (a pure display helper in C++),
// and the only "generation" this page does is drawing straight grid
// lines from the same numbers -- MeshGeometry.cpp itself does the real
// meshing, at run time, from whatever was last saved.
ColumnLayout {
    id: root
    spacing: 10

    readonly property var mesh: simulationController.meshConfig
    readonly property var geometry: simulationController.geometryConfig
    readonly property bool hasCase: Object.keys(mesh).length > 0
    readonly property var cellInfo: hasCase
        ? simulationController.meshCellInfo(
              { nx: parseInt(nxField.text) || 0, ny: parseInt(nyField.text) || 0 },
              { length: parseFloat(lengthField.text) || 0, height: parseFloat(heightField.text) || 0 })
        : ({})

    function commit() {
        simulationController.setMeshAndGeometry(
            { type: mesh.type, nx: parseInt(nxField.text) || 0, ny: parseInt(nyField.text) || 0 },
            { type: geometry.type, length: parseFloat(lengthField.text) || 0,
              height: parseFloat(heightField.text) || 0 })
        preview.requestPaint()
    }

    Label {
        text: "No case is open. Use New Case or Open on the Case page first."
        visible: !hasCase
    }

    RowLayout {
        visible: hasCase
        Layout.fillWidth: true
        spacing: 16

        GroupBox {
            title: "Domain"
            Layout.preferredWidth: 260
            ColumnLayout {
                anchors.fill: parent
                RowLayout {
                    Label { text: "Width (length)"; Layout.preferredWidth: 110 }
                    TextField {
                        id: lengthField
                        Layout.fillWidth: true
                        text: hasCase ? geometry.length : ""
                        validator: DoubleValidator { bottom: 0; notation: DoubleValidator.StandardNotation }
                        onEditingFinished: commit()
                    }
                }
                RowLayout {
                    Label { text: "Height"; Layout.preferredWidth: 110 }
                    TextField {
                        id: heightField
                        Layout.fillWidth: true
                        text: hasCase ? geometry.height : ""
                        validator: DoubleValidator { bottom: 0; notation: DoubleValidator.StandardNotation }
                        onEditingFinished: commit()
                    }
                }
                RowLayout {
                    Label { text: "nx (cells)"; Layout.preferredWidth: 110 }
                    TextField {
                        id: nxField
                        Layout.fillWidth: true
                        text: hasCase ? mesh.nx : ""
                        validator: IntValidator { bottom: 0 }
                        onEditingFinished: commit()
                    }
                }
                RowLayout {
                    Label { text: "ny (cells)"; Layout.preferredWidth: 110 }
                    TextField {
                        id: nyField
                        Layout.fillWidth: true
                        text: hasCase ? mesh.ny : ""
                        validator: IntValidator { bottom: 0 }
                        onEditingFinished: commit()
                    }
                }
                Label { text: "Mesh type: " + (hasCase ? mesh.type : "") }
            }
        }

        GroupBox {
            title: "Derived"
            Layout.preferredWidth: 220
            ColumnLayout {
                anchors.fill: parent
                Label { text: "Total cells: " + (cellInfo.cellCount !== undefined ? cellInfo.cellCount : "-") }
                Label { text: "dx: " + (cellInfo.dx !== undefined ? cellInfo.dx.toPrecision(5) : "-") }
                Label { text: "dy: " + (cellInfo.dy !== undefined ? cellInfo.dy.toPrecision(5) : "-") }
                Label {
                    text: "Warning: large mesh (" + (cellInfo.cellCount !== undefined ? cellInfo.cellCount : 0) + " cells)"
                    color: "#b06000"
                    wrapMode: Text.WrapAnywhere
                    visible: cellInfo.isLarge === true
                }
            }
        }

        GroupBox {
            title: "Preview"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Canvas {
                id: preview
                anchors.fill: parent
                anchors.margins: 6
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = "#ffffff"
                    ctx.fillRect(0, 0, width, height)
                    var nx = parseInt(nxField.text) || 0
                    var ny = parseInt(nyField.text) || 0
                    var w = parseFloat(lengthField.text) || 0
                    var h = parseFloat(heightField.text) || 0
                    if (nx <= 0 || ny <= 0 || w <= 0 || h <= 0) return

                    // Fit the domain's own aspect ratio inside the canvas,
                    // centered -- this draws proportions only, never a
                    // meshing algorithm (MeshGeometry::createCartesian2D
                    // does the real, solver-facing meshing at run time).
                    var margin = 4
                    var availW = width - 2 * margin
                    var availH = height - 2 * margin
                    var scale = Math.min(availW / w, availH / h)
                    var drawW = w * scale
                    var drawH = h * scale
                    var ox = (width - drawW) / 2
                    var oy = (height - drawH) / 2

                    ctx.strokeStyle = "#9e9e9e"
                    ctx.lineWidth = 1
                    ctx.strokeRect(ox, oy, drawW, drawH)

                    // Cap the number of drawn lines so an extreme nx/ny
                    // (e.g. the "large mesh" warning range) never turns
                    // this preview into a multi-second solid-fill paint --
                    // still visually representative of a fine grid.
                    var maxLines = 200
                    var stepX = Math.max(1, Math.ceil(nx / maxLines))
                    var stepY = Math.max(1, Math.ceil(ny / maxLines))

                    ctx.strokeStyle = "#cfd8dc"
                    ctx.beginPath()
                    for (var i = stepX; i < nx; i += stepX) {
                        var px = ox + (i / nx) * drawW
                        ctx.moveTo(px, oy)
                        ctx.lineTo(px, oy + drawH)
                    }
                    for (var j = stepY; j < ny; j += stepY) {
                        var py = oy + (j / ny) * drawH
                        ctx.moveTo(ox, py)
                        ctx.lineTo(ox + drawW, py)
                    }
                    ctx.stroke()
                }
            }
        }
    }

    RowLayout {
        visible: hasCase
        Button { text: "Apply && Validate"; onClicked: { commit(); simulationController.validateDraft() } }
    }

    ValidationPanel { Layout.fillWidth: true; visible: hasCase }

    Item { Layout.fillHeight: true }

    Connections {
        target: simulationController
        function onCaseChanged() { preview.requestPaint() }
    }
    Component.onCompleted: preview.requestPaint()
}
