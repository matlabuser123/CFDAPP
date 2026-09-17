import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// GUI-002 -- Mesh Editor: edits cfd::io::MeshConfig + GeometryConfig
// (the only two sections a structured-Cartesian case's grid spans) via
// simulationController.setMeshAndGeometry() -- never a QML-local copy of
// these values (meshConfig()/geometryConfig() re-read straight from the
// authoritative CaseSession after every commit, the same "one model, not
// one copy per page" convention every editor page here follows). No
// solver/meshing logic lives here: nx*ny/dx/dy (and, P12-MESH-002, the
// graded node positions and min/max cell sizes) come from
// simulationController.meshCellInfo() (a pure display helper in C++,
// calling the same grading function the solver's mesh uses), and the only
// "generation" this page does is drawing grid lines at those positions --
// MeshGeometry.cpp itself does the real meshing, at run time, from
// whatever was last saved.
ColumnLayout {
    id: root
    spacing: 10

    readonly property var mesh: simulationController.meshConfig
    readonly property var geometry: simulationController.geometryConfig
    readonly property bool hasCase: Object.keys(mesh).length > 0
    // P12-MESH-006: a geometry.json "box" is a 3D case: depth and nz are edited too (uniform
    // Cartesian hexahedra only -- no grading with nz).
    readonly property bool isBox: hasCase && geometry.type === "box"
    // P12-MESH-002: grading applies to structured_cartesian only (2D).
    readonly property bool canGrade: hasCase && mesh.type === "structured_cartesian" && !isBox
    // P12-MESH-003: a multiblock mesh (general 2D geometry) is defined
    // entirely by mesh.json -- shown read-only (blocks, cells), not edited;
    // the rectangle fields, grading and preview below do not apply to it.
    readonly property bool isMultiBlock: hasCase && mesh.type === "multiblock"
    function blockSummary() {
        if (!isMultiBlock || mesh.blocks === undefined) return ""
        var parts = []
        for (var k = 0; k < mesh.blocks.length; ++k) {
            parts.push(mesh.blocks[k].name + " " + mesh.blocks[k].nx + " x " + mesh.blocks[k].ny)
        }
        return parts.join(", ")
    }
    readonly property var cellInfo: hasCase
        ? simulationController.meshCellInfo(meshMap(), geometryMap())
        : ({})

    // The editor's current geometry in CaseModelAdapter's form (a box adds its depth).
    function geometryMap() {
        var g = { type: geometry.type, length: parseFloat(lengthField.text) || 0,
                  height: parseFloat(heightField.text) || 0 }
        if (isBox) g.depth = parseFloat(depthField.text) || 0
        return g
    }

    // The editor's current mesh settings in CaseModelAdapter's flattened
    // form (type/nx/ny[/nz] + per-axis grading type/ratio/cluster).
    function meshMap() {
        var m = { type: mesh.type, nx: parseInt(nxField.text) || 0, ny: parseInt(nyField.text) || 0 }
        if (isBox) m.nz = parseInt(nzField.text) || 0
        if (canGrade) {
            m.xGradingType = xGradingType.currentText
            m.xGradingRatio = parseFloat(xGradingRatio.text) || 0
            m.xGradingCluster = xGradingCluster.currentText
            m.yGradingType = yGradingType.currentText
            m.yGradingRatio = parseFloat(yGradingRatio.text) || 0
            m.yGradingCluster = yGradingCluster.currentText
        }
        return m
    }

    function commit() {
        // (A multiblock mesh has nothing editable here; the adapter keeps its
        // blocks, and a mesh_defined geometry has no length/height.)
        simulationController.setMeshAndGeometry(meshMap(), geometryMap())
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
            // P12-MESH-003: read-only summary of a multiblock mesh.
            title: "Multi-block mesh"
            visible: isMultiBlock
            Layout.preferredWidth: 360
            ColumnLayout {
                anchors.fill: parent
                Label {
                    text: "General 2D geometry from mesh.json (geometry type " + (hasCase ? geometry.type : "")
                          + "): " + (isMultiBlock ? mesh.blocks.length : 0) + " blocks, "
                          + (isMultiBlock ? mesh.interfaceCount : 0) + " interfaces, "
                          + (isMultiBlock ? mesh.cellCount : 0) + " cells."
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: "Blocks: " + blockSummary()
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: "Blocks, interfaces and boundary patches are edited in mesh.json, not here; "
                          + "Apply && Validate checks the whole case. Results are exported to VTK."
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        GroupBox {
            title: "Domain"
            visible: !isMultiBlock
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
                    // P12-MESH-006: the z extent of a 3D box.
                    visible: isBox
                    Label { text: "Depth"; Layout.preferredWidth: 110 }
                    TextField {
                        id: depthField
                        Layout.fillWidth: true
                        text: isBox ? geometry.depth : ""
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
                RowLayout {
                    // P12-MESH-006: cells along z of a 3D box.
                    visible: isBox
                    Label { text: "nz (cells)"; Layout.preferredWidth: 110 }
                    TextField {
                        id: nzField
                        Layout.fillWidth: true
                        text: isBox ? mesh.nz : ""
                        validator: IntValidator { bottom: 0 }
                        onEditingFinished: commit()
                    }
                }
                Label { text: "Mesh type: " + (hasCase ? mesh.type : "") + (isBox ? " (3D hexahedra)" : "") }
                Label {
                    visible: isBox
                    text: "3D box: uniform Cartesian hexahedra (patches xmin..zmax); laminar incompressible "
                          + "flow only. The preview shows the x-y face."
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    // P12-MESH-001: vertex-defined meshes come from mesh.json.
                    visible: hasCase && mesh.type === "structured_quad"
                    text: "Vertex-defined mesh (" + (hasCase ? mesh.vertexCount : 0) + " vertices from mesh.json); "
                          + "nx/ny must match them. Preview and dx/dy are schematic."
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        GroupBox {
            // P12-MESH-002: cell-size grading per axis (mesh.json "grading").
            title: "Grading"
            visible: canGrade
            Layout.preferredWidth: 300
            GridLayout {
                anchors.fill: parent
                columns: 4
                Label { text: "" }
                Label { text: "type" }
                Label { text: "ratio" }
                Label { text: "cluster" }

                Label { text: "x" }
                ComboBox {
                    id: xGradingType
                    model: ["uniform", "geometric"]
                    currentIndex: hasCase && mesh.xGradingType === "geometric" ? 1 : 0
                    onActivated: commit()
                }
                TextField {
                    id: xGradingRatio
                    Layout.preferredWidth: 60
                    enabled: xGradingType.currentIndex === 1
                    text: hasCase && mesh.xGradingRatio !== undefined ? mesh.xGradingRatio : "1"
                    validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
                    onEditingFinished: commit()
                }
                ComboBox {
                    id: xGradingCluster
                    enabled: xGradingType.currentIndex === 1
                    model: ["left", "right", "both"]
                    currentIndex: hasCase ? Math.max(0, model.indexOf(mesh.xGradingCluster)) : 2
                    onActivated: commit()
                }

                Label { text: "y" }
                ComboBox {
                    id: yGradingType
                    model: ["uniform", "geometric"]
                    currentIndex: hasCase && mesh.yGradingType === "geometric" ? 1 : 0
                    onActivated: commit()
                }
                TextField {
                    id: yGradingRatio
                    Layout.preferredWidth: 60
                    enabled: yGradingType.currentIndex === 1
                    text: hasCase && mesh.yGradingRatio !== undefined ? mesh.yGradingRatio : "1"
                    validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
                    onEditingFinished: commit()
                }
                ComboBox {
                    id: yGradingCluster
                    enabled: yGradingType.currentIndex === 1
                    model: ["bottom", "top", "both"]
                    currentIndex: hasCase ? Math.max(0, model.indexOf(mesh.yGradingCluster)) : 2
                    onActivated: commit()
                }

                Label {
                    Layout.columnSpan: 4
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "ratio = growth factor of adjacent cell widths away from the clustered "
                          + "boundary (>= 1); cluster = where the small cells go."
                }
            }
        }

        GroupBox {
            title: "Derived"
            visible: !isMultiBlock
            Layout.preferredWidth: 220
            ColumnLayout {
                anchors.fill: parent
                Label { text: "Total cells: " + (cellInfo.cellCount !== undefined ? cellInfo.cellCount : "-") }
                Label {
                    text: cellInfo.minXWidth !== undefined && cellInfo.minXWidth !== cellInfo.maxXWidth
                          ? "dx: " + cellInfo.minXWidth.toPrecision(4) + " .. " + cellInfo.maxXWidth.toPrecision(4)
                          : "dx: " + (cellInfo.dx !== undefined ? cellInfo.dx.toPrecision(5) : "-")
                }
                Label {
                    text: cellInfo.minYWidth !== undefined && cellInfo.minYWidth !== cellInfo.maxYWidth
                          ? "dy: " + cellInfo.minYWidth.toPrecision(4) + " .. " + cellInfo.maxYWidth.toPrecision(4)
                          : "dy: " + (cellInfo.dy !== undefined ? cellInfo.dy.toPrecision(5) : "-")
                }
                Label {
                    visible: isBox
                    text: "dz: " + (cellInfo.dz !== undefined ? cellInfo.dz.toPrecision(5) : "-")
                }
                Label {
                    text: cellInfo.gradingError !== undefined ? cellInfo.gradingError : ""
                    color: "#b00020"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    visible: cellInfo.gradingError !== undefined
                }
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
            visible: !isMultiBlock
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

                    // P12-MESH-002: graded node positions (fractions of the
                    // axis, from meshCellInfo -- the real grading function)
                    // when available, uniform otherwise. y grows upward.
                    var xNodes = root.cellInfo.xNodes
                    var yNodes = root.cellInfo.yNodes
                    var xAt = function(i) {
                        return (xNodes !== undefined && xNodes.length === nx + 1) ? xNodes[i] : i / nx
                    }
                    var yAt = function(j) {
                        return (yNodes !== undefined && yNodes.length === ny + 1) ? yNodes[j] : j / ny
                    }
                    ctx.strokeStyle = "#cfd8dc"
                    ctx.beginPath()
                    for (var i = stepX; i < nx; i += stepX) {
                        var px = ox + xAt(i) * drawW
                        ctx.moveTo(px, oy)
                        ctx.lineTo(px, oy + drawH)
                    }
                    for (var j = stepY; j < ny; j += stepY) {
                        var py = oy + drawH - yAt(j) * drawH
                        ctx.moveTo(ox, py)
                        ctx.lineTo(ox + drawW, py)
                    }
                    ctx.stroke()
                }
            }
        }
    }

    // P12-MESH-004: the production mesh-quality report of the open case
    // (simulationController.meshQuality -- MeshQuality::evaluate via the
    // real CaseBuilder, refreshed on open and on Apply & Validate).
    GroupBox {
        id: qualityBox
        title: "Mesh quality"
        visible: hasCase && simulationController.meshQuality.status !== undefined
        Layout.fillWidth: true
        readonly property var q: simulationController.meshQuality
        ColumnLayout {
            anchors.fill: parent
            Label {
                text: "Status: " + (qualityBox.q.status || "")
                font.bold: true
                color: qualityBox.q.status === "valid" ? "#2e7d32"
                       : qualityBox.q.status === "valid_with_warnings" ? "#b06000" : "#b00020"
            }
            Label {
                text: qualityBox.q.summary || ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Repeater {
                model: qualityBox.q.issues || []
                delegate: Label {
                    text: modelData.text
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    color: modelData.severity === "fatal" ? "#b00020"
                           : modelData.severity === "warning" ? "#b06000" : "#455a64"
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
