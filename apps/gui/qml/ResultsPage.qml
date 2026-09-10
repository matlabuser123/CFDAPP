import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// P5-B/GUI visualization integration -- unchanged from the original
// single-page Main.qml (moved here verbatim as part of the GUI-006
// navigation-shell split): the result viewport (scalar field + contour
// overlay + vector glyphs + probe), line-sampling panel, and residual-
// history plot. Every number drawn here comes from simulationController's
// own Q_INVOKABLE methods, which only ever adapt the already-tested
// include/cfd/viz/ algorithms -- this file draws what it's given, it
// never computes a contour, a vector, a probe answer, or a residual
// itself.
ColumnLayout {
    id: root
    spacing: 4

    property string selectedField: ""
    property bool showContours: false
    property int contourLevels: 6
    property bool showVectors: false
    property int vectorStride: 1
    property real vectorScale: 0.08
    property var probeResult: ({})
    property var lineSampleResult: []
    property real lineX0: 0
    property real lineY0: 0.5
    property real lineX1: 1
    property real lineY1: 0.5

    function refreshFieldList() {
        var fields = simulationController.availableFields
        fieldSelector.model = fields
        if (fields.length > 0 && fields.indexOf(selectedField) === -1) {
            selectedField = fields[0]
        } else if (fields.length === 0) {
            selectedField = ""
        }
    }

    function refreshAll() {
        refreshFieldList()
        resultCanvas.requestPaint()
        residualCanvas.requestPaint()
    }

    Connections {
        target: simulationController
        function onResultsChanged() { refreshAll() }
        function onCaseChanged() { refreshAll() }
    }

    Component.onCompleted: refreshAll()

    function colorFor(t) {
        t = Math.max(0, Math.min(1, t))
        var r = Math.round(255 * Math.min(1, Math.max(0, 1.5 - Math.abs(4 * t - 3))))
        var g = Math.round(255 * Math.min(1, Math.max(0, 1.5 - Math.abs(4 * t - 2))))
        var b = Math.round(255 * Math.min(1, Math.max(0, 1.5 - Math.abs(4 * t - 1))))
        return Qt.rgba(r / 255, g / 255, b / 255, 1)
    }
    function domainToPixelX(x, bounds, w) {
        var span = bounds.maxX - bounds.minX
        return span > 0 ? (x - bounds.minX) / span * w : w / 2
    }
    function domainToPixelY(y, bounds, h) {
        var span = bounds.maxY - bounds.minY
        return span > 0 ? h - (y - bounds.minY) / span * h : h / 2
    }
    function pixelToDomainX(px, bounds, w) {
        return bounds.minX + (px / w) * (bounds.maxX - bounds.minX)
    }
    function pixelToDomainY(py, bounds, h) {
        return bounds.minY + (1 - py / h) * (bounds.maxY - bounds.minY)
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            Label {
                text: simulationController.hasResults ? "" : "No results yet -- run a case or open one with existing results."
                visible: !simulationController.hasResults
            }

            Canvas {
                id: resultCanvas
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: simulationController.hasResults

                property var bounds: simulationController.meshBounds

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = "#f5f5f5"
                    ctx.fillRect(0, 0, width, height)
                    if (!simulationController.hasResults || selectedField.length === 0) return

                    var grid = simulationController.scalarFieldGrid(selectedField)
                    if (!grid.nx || !grid.ny || !grid.values) return
                    var nx = grid.nx, ny = grid.ny
                    var minV = grid.minValue, maxV = grid.maxValue
                    var span = maxV - minV
                    var cw = width / nx, ch = height / ny
                    var values = grid.values
                    for (var j = 0; j < ny; ++j) {
                        for (var i = 0; i < nx; ++i) {
                            var v = values[j * nx + i]
                            var t = span > 0 ? (v - minV) / span : 0.5
                            ctx.fillStyle = colorFor(t)
                            var x = i * cw
                            var y = height - (j + 1) * ch
                            ctx.fillRect(x, y, cw + 1, ch + 1)
                        }
                    }

                    if (showContours) {
                        var b = bounds
                        var segs = simulationController.contourSegments(selectedField, contourLevels)
                        ctx.strokeStyle = "#000000"
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        for (var s = 0; s < segs.length; ++s) {
                            var seg = segs[s]
                            ctx.moveTo(domainToPixelX(seg.x1, b, width), domainToPixelY(seg.y1, b, height))
                            ctx.lineTo(domainToPixelX(seg.x2, b, width), domainToPixelY(seg.y2, b, height))
                        }
                        ctx.stroke()
                    }

                    if (showVectors) {
                        var bb = bounds
                        var domainSpanX = Math.max(bb.maxX - bb.minX, 1e-12)
                        var vectors = simulationController.vectorSamples(vectorStride)
                        ctx.strokeStyle = "#202020"
                        ctx.lineWidth = 1
                        for (var k = 0; k < vectors.length; ++k) {
                            var vec = vectors[k]
                            var px = domainToPixelX(vec.x, bb, width)
                            var py = domainToPixelY(vec.y, bb, height)
                            var dx = vec.vx * vectorScale / domainSpanX * width
                            var dy = -vec.vy * vectorScale / domainSpanX * width
                            ctx.beginPath()
                            ctx.moveTo(px, py)
                            ctx.lineTo(px + dx, py + dy)
                            ctx.stroke()
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: function(mouse) {
                        if (!simulationController.hasResults) return
                        var b = resultCanvas.bounds
                        var x = pixelToDomainX(mouse.x, b, width)
                        var y = pixelToDomainY(mouse.y, b, height)
                        probeResult = simulationController.probeAt(x, y)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Probe: " }
                Label {
                    Layout.fillWidth: true
                    text: {
                        var keys = Object.keys(probeResult)
                        if (keys.length === 0) return "(click the viewport)"
                        var parts = []
                        for (var i = 0; i < keys.length; ++i) parts.push(keys[i] + "=" + Number(probeResult[keys[i]]).toPrecision(4))
                        return parts.join("  ")
                    }
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            spacing: 6
            enabled: simulationController.hasResults

            RowLayout {
                Button {
                    text: "Reload results"
                    enabled: simulationController.caseDirectory.length > 0
                    onClicked: simulationController.loadCompletedResults()
                }
            }

            GroupBox {
                title: "Field"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    ComboBox {
                        id: fieldSelector
                        Layout.fillWidth: true
                        onCurrentTextChanged: if (currentText.length > 0) { selectedField = currentText; resultCanvas.requestPaint() }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: {
                            if (!simulationController.hasResults || selectedField.length === 0) return ""
                            var g = simulationController.scalarFieldGrid(selectedField)
                            return "min " + Number(g.minValue).toPrecision(4) + "  max " + Number(g.maxValue).toPrecision(4)
                        }
                    }
                }
            }

            GroupBox {
                title: "Contours"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    CheckBox { text: "Show"; checked: showContours; onCheckedChanged: { showContours = checked; resultCanvas.requestPaint() } }
                    RowLayout {
                        Label { text: "Levels" }
                        SpinBox {
                            from: 1; to: 20; value: contourLevels
                            onValueChanged: { contourLevels = value; resultCanvas.requestPaint() }
                        }
                    }
                }
            }

            GroupBox {
                title: "Vectors"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    CheckBox { text: "Show"; checked: showVectors; onCheckedChanged: { showVectors = checked; resultCanvas.requestPaint() } }
                    RowLayout {
                        Label { text: "Stride" }
                        SpinBox {
                            from: 1; to: 20; value: vectorStride
                            onValueChanged: { vectorStride = value; resultCanvas.requestPaint() }
                        }
                    }
                    RowLayout {
                        Label { text: "Scale" }
                        Slider {
                            from: 0.01; to: 0.5; value: vectorScale
                            onValueChanged: { vectorScale = value; resultCanvas.requestPaint() }
                        }
                    }
                }
            }

            GroupBox {
                title: "Line sample"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    RowLayout {
                        TextField { placeholderText: "x0"; text: lineX0; onEditingFinished: lineX0 = parseFloat(text) || 0 }
                        TextField { placeholderText: "y0"; text: lineY0; onEditingFinished: lineY0 = parseFloat(text) || 0 }
                    }
                    RowLayout {
                        TextField { placeholderText: "x1"; text: lineX1; onEditingFinished: lineX1 = parseFloat(text) || 0 }
                        TextField { placeholderText: "y1"; text: lineY1; onEditingFinished: lineY1 = parseFloat(text) || 0 }
                    }
                    Button {
                        text: "Sample"
                        Layout.fillWidth: true
                        onClicked: lineSampleResult = simulationController.sampleLine(selectedField, lineX0, lineY0, lineX1, lineY1, 20)
                    }
                    Button {
                        text: "Export CSV"
                        Layout.fillWidth: true
                        enabled: lineSampleResult.length > 0
                        onClicked: simulationController.exportLineSampleCsv(
                            simulationController.caseDirectory + "/results/line_sample.csv",
                            selectedField, lineX0, lineY0, lineX1, lineY1, 20)
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                        text: lineSampleResult.length > 0 ? (lineSampleResult.length + " samples (see Export CSV)") : ""
                    }
                }
            }
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 160
        Canvas {
            id: residualCanvas
            anchors.fill: parent
            anchors.margins: 8

            property var seriesNames: []
            property var seriesColors: ({ "u": "#d62728", "v": "#1f77b4", "pressure": "#2ca02c", "continuity": "#9467bd" })

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = "#ffffff"
                ctx.fillRect(0, 0, width, height)
                if (!simulationController.hasResults) return

                var names = simulationController.availableResidualSeriesNames
                var allSeries = {}
                var maxLen = 0
                var minLog = 1e300, maxLog = -1e300
                for (var n = 0; n < names.length; ++n) {
                    var values = simulationController.residualSeries(names[n])
                    allSeries[names[n]] = values
                    maxLen = Math.max(maxLen, values.length)
                    for (var i = 0; i < values.length; ++i) {
                        var lv = Math.log(values[i]) / Math.LN10
                        minLog = Math.min(minLog, lv)
                        maxLog = Math.max(maxLog, lv)
                    }
                }
                if (maxLen < 2 || maxLog <= minLog) return

                for (var s = 0; s < names.length; ++s) {
                    var series = allSeries[names[s]]
                    ctx.strokeStyle = seriesColors[names[s]] || "#000000"
                    ctx.lineWidth = 1.5
                    ctx.beginPath()
                    for (var k = 0; k < series.length; ++k) {
                        var px = (k / (maxLen - 1)) * width
                        var lv2 = Math.log(series[k]) / Math.LN10
                        var py = height - ((lv2 - minLog) / (maxLog - minLog)) * height
                        if (k === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                    }
                    ctx.stroke()
                }
            }
        }
    }
}
