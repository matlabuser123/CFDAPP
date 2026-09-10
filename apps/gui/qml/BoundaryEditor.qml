import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// GUI-004 -- Boundary-Condition Editor: edits cfd::io::BoundaryConfig
// (the four structured patches -- left/right/top/bottom, the exact
// vocabulary CaseModelAdapter.cpp's own boundaryConfigFromVariant() walks)
// via simulationController.setBoundaryConfig(). A patch selector on the
// left plus a highlighted rectangle preview, a field-specific editor on
// the right -- "Boundary list/diagram -> selected boundary -> field-
// specific BC editor". Velocity/pressure/temperature/species/alpha type
// dropdowns are populated from simulationController.velocityBoundaryTypes()
// etc. (cfd/io/case/BoundaryVocabulary.hpp -- the exact vocabulary
// BoundaryConfigParser.cpp/CaseBuilder.cpp accept), never a hand-typed
// second list.
ColumnLayout {
    id: root
    spacing: 10

    readonly property var boundaries: simulationController.boundaryConfig
    readonly property var physics: simulationController.physicsConfig
    readonly property bool hasCase: Object.keys(boundaries).length > 0
    readonly property bool thermalEnabled: physics.thermal !== undefined
    readonly property bool multiphaseEnabled: physics.multiphase !== undefined
    readonly property var speciesNames: physics.species ? physics.species.map(function(s) { return s.name }) : []
    property string selectedPatch: "left"
    property var draft: ({})

    function resetDraft() { draft = JSON.parse(JSON.stringify(simulationController.boundaryConfig)) }
    function commit() { simulationController.setBoundaryConfig(draft); draft = draft }
    function patch() { return draft[selectedPatch] || {}; }

    Connections {
        target: simulationController
        function onCaseChanged() { resetDraft() }
    }
    Component.onCompleted: resetDraft()

    Label { text: "No case is open. Use New Case or Open on the Case page first."; visible: !hasCase }

    RowLayout {
        visible: hasCase
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 16

        // --- Patch selector + domain preview -----------------------------
        ColumnLayout {
            Layout.preferredWidth: 220
            GroupBox {
                title: "Boundary"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Repeater {
                        model: ["left", "right", "top", "bottom"]
                        delegate: Button {
                            Layout.fillWidth: true
                            text: modelData
                            highlighted: selectedPatch === modelData
                            onClicked: selectedPatch = modelData
                        }
                    }
                }
            }
            GroupBox {
                title: "Domain"
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                Canvas {
                    id: domainPreview
                    anchors.fill: parent
                    anchors.margins: 8
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        ctx.fillStyle = "#ffffff"
                        ctx.fillRect(0, 0, width, height)
                        var m = 10
                        ctx.strokeStyle = "#9e9e9e"
                        ctx.lineWidth = 2
                        ctx.strokeRect(m, m, width - 2 * m, height - 2 * m)

                        ctx.strokeStyle = "#1565c0"
                        ctx.lineWidth = 5
                        ctx.beginPath()
                        if (selectedPatch === "left") { ctx.moveTo(m, m); ctx.lineTo(m, height - m) }
                        else if (selectedPatch === "right") { ctx.moveTo(width - m, m); ctx.lineTo(width - m, height - m) }
                        else if (selectedPatch === "top") { ctx.moveTo(m, m); ctx.lineTo(width - m, m) }
                        else { ctx.moveTo(m, height - m); ctx.lineTo(width - m, height - m) }
                        ctx.stroke()
                    }
                }
                Connections {
                    target: root
                    function onSelectedPatchChanged() { domainPreview.requestPaint() }
                }
                Component.onCompleted: domainPreview.requestPaint()
            }
        }

        // --- Field-specific editor for the selected patch -----------------
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: fieldsColumn.implicitHeight
            clip: true

            ColumnLayout {
                id: fieldsColumn
                width: parent.width
                spacing: 10

                GroupBox {
                    title: selectedPatch + " / Velocity"
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        RowLayout {
                            Label { text: "Type"; Layout.preferredWidth: 100 }
                            ComboBox {
                                id: velocityTypeBox
                                model: simulationController.velocityBoundaryTypes()
                                currentIndex: model.indexOf(patch().velocity ? patch().velocity.type : "wall")
                                onActivated: {
                                    var p = patch()
                                    p.velocity = p.velocity || { valueX: 0, valueY: 0 }
                                    p.velocity.type = model[currentIndex]
                                    draft[selectedPatch] = p
                                    commit()
                                }
                            }
                        }
                        RowLayout {
                            visible: patch().velocity && simulationController.velocityTypeHasValue(patch().velocity.type)
                            Label { text: "Value X / Y"; Layout.preferredWidth: 100 }
                            TextField {
                                Layout.preferredWidth: 90
                                text: patch().velocity ? patch().velocity.valueX : 0
                                validator: DoubleValidator {}
                                onEditingFinished: { patch().velocity.valueX = parseFloat(text) || 0; commit() }
                            }
                            TextField {
                                Layout.preferredWidth: 90
                                text: patch().velocity ? patch().velocity.valueY : 0
                                validator: DoubleValidator {}
                                onEditingFinished: { patch().velocity.valueY = parseFloat(text) || 0; commit() }
                            }
                        }
                    }
                }

                GroupBox {
                    title: selectedPatch + " / Pressure"
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        RowLayout {
                            Label { text: "Type"; Layout.preferredWidth: 100 }
                            ComboBox {
                                model: simulationController.pressureBoundaryTypes()
                                currentIndex: model.indexOf(patch().pressure ? patch().pressure.type : "fixed_gradient")
                                onActivated: {
                                    var p = patch()
                                    p.pressure = p.pressure || { value: 0 }
                                    p.pressure.type = model[currentIndex]
                                    draft[selectedPatch] = p
                                    commit()
                                }
                            }
                        }
                        RowLayout {
                            Label { text: "Value"; Layout.preferredWidth: 100 }
                            TextField {
                                text: patch().pressure ? patch().pressure.value : 0
                                validator: DoubleValidator {}
                                onEditingFinished: { patch().pressure.value = parseFloat(text) || 0; commit() }
                            }
                        }
                    }
                }

                GroupBox {
                    title: selectedPatch + " / Temperature"
                    Layout.fillWidth: true
                    visible: thermalEnabled
                    ColumnLayout {
                        anchors.fill: parent
                        RowLayout {
                            Label { text: "Type"; Layout.preferredWidth: 100 }
                            ComboBox {
                                id: temperatureTypeBox
                                model: simulationController.temperatureBoundaryTypes()
                                currentIndex: model.indexOf(patch().temperature ? patch().temperature.type : "adiabatic")
                                onActivated: {
                                    var p = patch()
                                    p.temperature = p.temperature || { value: 0 }
                                    p.temperature.type = model[currentIndex]
                                    draft[selectedPatch] = p
                                    commit()
                                }
                            }
                        }
                        RowLayout {
                            visible: patch().temperature && simulationController.temperatureTypeHasValue(patch().temperature.type)
                            Label { text: "Value"; Layout.preferredWidth: 100 }
                            TextField {
                                text: patch().temperature ? patch().temperature.value : 0
                                validator: DoubleValidator {}
                                onEditingFinished: { patch().temperature.value = parseFloat(text) || 0; commit() }
                            }
                        }
                    }
                }

                GroupBox {
                    title: selectedPatch + " / Species"
                    Layout.fillWidth: true
                    visible: speciesNames.length > 0
                    ColumnLayout {
                        anchors.fill: parent
                        Repeater {
                            model: speciesNames
                            delegate: RowLayout {
                                readonly property var spec: patch().species ? patch().species[modelData] : undefined
                                Label { text: modelData; Layout.preferredWidth: 90 }
                                ComboBox {
                                    id: speciesTypeBox
                                    model: simulationController.pressureBoundaryTypes()
                                    currentIndex: model.indexOf(spec ? spec.type : "fixed_value")
                                    onActivated: {
                                        var p = patch()
                                        p.species = p.species || {}
                                        p.species[modelData] = p.species[modelData] || { value: 0 }
                                        p.species[modelData].type = model[currentIndex]
                                        draft[selectedPatch] = p
                                        commit()
                                    }
                                }
                                TextField {
                                    Layout.preferredWidth: 90
                                    text: spec ? spec.value : 0
                                    validator: DoubleValidator {}
                                    onEditingFinished: {
                                        var p = patch()
                                        p.species = p.species || {}
                                        p.species[modelData] = p.species[modelData] || { type: "fixed_value" }
                                        p.species[modelData].value = parseFloat(text) || 0
                                        draft[selectedPatch] = p
                                        commit()
                                    }
                                }
                                Label {
                                    text: spec ? "" : "missing"
                                    color: "#b00020"
                                    visible: !spec
                                }
                            }
                        }
                    }
                }

                GroupBox {
                    title: selectedPatch + " / Alpha (multiphase)"
                    Layout.fillWidth: true
                    visible: multiphaseEnabled
                    ColumnLayout {
                        anchors.fill: parent
                        RowLayout {
                            Label { text: "Type"; Layout.preferredWidth: 100 }
                            ComboBox {
                                id: alphaTypeBox
                                model: simulationController.pressureBoundaryTypes()
                                currentIndex: model.indexOf(patch().alpha ? patch().alpha.type : "fixed_value")
                                onActivated: {
                                    var p = patch()
                                    p.alpha = p.alpha || { value: 0.5 }
                                    p.alpha.type = model[currentIndex]
                                    draft[selectedPatch] = p
                                    commit()
                                }
                            }
                        }
                        RowLayout {
                            Label { text: "Value (0-1 if fixed_value)"; Layout.preferredWidth: 160 }
                            TextField {
                                text: patch().alpha ? patch().alpha.value : 0
                                validator: DoubleValidator {}
                                onEditingFinished: { patch().alpha.value = parseFloat(text) || 0; commit() }
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
}
