import QtQuick 6.8
import QtQuick.Window 6.8
import QtQuick.Controls 6.8

Window {
    id: panel
    width: 620
    height: 700
    minimumWidth: 440
    minimumHeight: 340
    title: frontend.panelTitle
    color: "transparent"
    // Qt.Dialog matches the proven Astra implementation and keeps this window
    // reliably activatable when launched from a native QMenu. Frameless preserves
    // the custom Shader Studio chrome.
    flags: Qt.Dialog | Qt.FramelessWindowHint
    modality: Qt.NonModal
    visible: false

    function subtitleFor(title) {
        if (title === "Output / Console") return "Compiler, package and runtime diagnostics"
        if (title === "Inputs") return "Resolved shader inputs and preview resources"
        if (title === "Parameters") return "Editable parameters reflected from the active shader"
        if (title === "Material Textures") return "Material image slots, bindings and preview textures"
        if (title === "Performance") return "Preview and shader performance information"
        if (title === "Scene / Lighting") return "Material preview scene, environment and lighting controls"
        if (title === "Script Vectors") return "Detected BO3 GenericsCBuffer script vectors"
        if (title === "Preview Settings") return "Preview mode and advanced runtime options"
        return "Shader Studio utility panel"
    }

    function placeNearParent() {
        // Do not query screen.availableGeometry here. That property is not part of
        // the Qt Quick Window Screen API used by this build; dereferencing it can
        // throw before panel.show(), making every View utility action appear dead.
        // The transient parent is already a top-level QML Window, so centering on
        // its desktop coordinates is both sufficient and portable.
        if (!transientParent) return
        x = Math.round(transientParent.x + Math.max(20, (transientParent.width - width) / 2))
        y = Math.round(transientParent.y + Math.max(34, (transientParent.height - height) / 2))
    }

    onVisibleChanged: frontend.panelModel.setActive(visible)

    Connections {
        target: frontend
        function onPanelOpenRequested() {
            if (frontend.panelTitle === "Output / Console") {
                panel.width = 820
                panel.height = 600
            } else if (frontend.panelTitle === "Script Vectors") {
                panel.width = 650
                panel.height = 720
            } else {
                panel.width = 620
                panel.height = 700
            }

            // Match Astra's proven ordering: make the dialog visible first.
            // Position/raise/activate on the next event-loop turn, after the
            // native View menu has released mouse/keyboard activation.
            panel.show()
            Qt.callLater(function() {
                panel.placeNearParent()
                panel.raise()
                panel.requestActivate()
                controls.positionViewAtBeginning()
            })
        }
    }

    Shortcut { sequence: "Esc"; onActivated: panel.hide() }

    GlassSurface {
        id: shell
        anchors.fill: parent
        cornerRadius: 17
        glassOpacity: 0.97
        highlighted: panel.active

        Rectangle {
            visible: frontend.themeName !== "Liquid Glass"
            anchors.fill: parent
            anchors.margins: 1
            radius: 16
            color: Qt.rgba(frontend.windowColor.r, frontend.windowColor.g, frontend.windowColor.b, 0.97)
        }

        Item {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 72

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.right: closeButton.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Text {
                    text: frontend.panelTitle
                    color: frontend.textColor
                    font.pixelSize: 17
                    font.bold: true
                    elide: Text.ElideRight
                    width: parent.width
                }
                Text {
                    text: panel.subtitleFor(frontend.panelTitle)
                    color: frontend.mutedColor
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    width: parent.width
                }
            }

            GlassButton {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: 34
                height: 34
                compact: true
                text: "×"
                onClicked: panel.hide()
            }

            MouseArea {
                anchors.left: parent.left
                anchors.right: closeButton.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.SizeAllCursor
                onPressed: panel.startSystemMove()
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                height: 1
                color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.09)
            }
        }

        ListView {
            id: controls
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.bottom: footer.top
            anchors.margins: 14
            spacing: 8
            clip: true
            model: frontend.panelModel
            boundsBehavior: Flickable.StopAtBounds
            interactive: false
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            // Match desktop editor behavior: scroll with the wheel/trackpad or
            // scrollbar, not by grabbing and dragging the panel contents.
            WheelHandler {
                target: null
                onWheel: function(event) {
                    var delta = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 2
                    var maxY = Math.max(0, controls.contentHeight - controls.height)
                    controls.contentY = Math.max(0, Math.min(maxY, controls.contentY - delta))
                    event.accepted = true
                }
            }

            delegate: Loader {
                id: row
                required property var control
                required property int index
                width: controls.width - 14
                property var controlState: control
                sourceComponent: control.kind === "button" ? buttonControl
                    : control.kind === "check" ? checkControl
                    : control.kind === "choice" ? choiceControl
                    : control.kind === "number" || control.kind === "text" ? entryControl
                    : control.kind === "slider" ? sliderControl
                    : control.kind === "output" ? outputControl : labelControl

                Component {
                    id: labelControl
                    Column {
                        width: row.width
                        spacing: 5
                        topPadding: row.controlState.kind === "heading" ? 8 : 1
                        TextEdit {
                            width: parent.width
                            text: row.controlState.text
                            color: row.controlState.kind === "heading" ? frontend.textColor : frontend.mutedColor
                            font.pixelSize: row.controlState.kind === "heading" ? 14 : 11
                            font.bold: row.controlState.kind === "heading"
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.AutoText
                        }
                        Rectangle {
                            visible: row.controlState.kind === "heading"
                            width: parent.width
                            height: 1
                            color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.09)
                        }
                    }
                }

                Component {
                    id: buttonControl
                    GlassButton {
                        width: row.width
                        height: 36
                        text: row.controlState.text
                        enabled: row.controlState.enabled
                        onClicked: frontend.panelModel.activate(row.index)
                    }
                }

                Component {
                    id: checkControl
                    Rectangle {
                        width: row.width
                        height: 42
                        radius: 9
                        color: Qt.rgba(frontend.panelColor.r, frontend.panelColor.g, frontend.panelColor.b,
                                       frontend.themeName === "Liquid Glass" ? 0.72 : 0.52)
                        border.width: 1
                        border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.08)
                        CheckBox {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            text: row.controlState.text
                            checked: row.controlState.value
                            enabled: row.controlState.enabled
                            palette.windowText: frontend.textColor
                            palette.highlight: frontend.accentColor
                            onClicked: frontend.panelModel.edit(row.index, checked)
                        }
                    }
                }

                Component {
                    id: choiceControl
                    Column {
                        width: row.width
                        spacing: 5
                        Text {
                            visible: text.length > 0
                            text: row.controlState.text
                            color: frontend.mutedColor
                            font.pixelSize: 10
                            font.weight: Font.Medium
                        }
                        ComboBox {
                            width: parent.width
                            height: 38
                            model: row.controlState.choices
                            currentIndex: row.controlState.value
                            enabled: row.controlState.enabled
                            palette.button: frontend.buttonColor
                            palette.buttonText: frontend.textColor
                            palette.text: frontend.textColor
                            palette.base: frontend.baseColor
                            palette.highlight: frontend.accentColor
                            onActivated: frontend.panelModel.edit(row.index, currentIndex)
                            wheelEnabled: false
                        }
                    }
                }

                Component {
                    id: entryControl
                    Column {
                        width: row.width
                        spacing: 5
                        Text {
                            text: row.controlState.text
                            visible: text.length > 0
                            color: frontend.mutedColor
                            font.pixelSize: 10
                            font.weight: Font.Medium
                        }
                        TextField {
                            id: entry
                            width: parent.width
                            height: 38
                            enabled: row.controlState.enabled
                            readOnly: row.controlState.readOnly
                            color: frontend.textColor
                            selectByMouse: true
                            property string displayValue: row.controlState.kind === "number"
                                ? Number(row.controlState.value).toFixed(row.controlState.decimals) : String(row.controlState.value)
                            text: displayValue
                            onDisplayValueChanged: if (!activeFocus) text = displayValue
                            background: Rectangle {
                                radius: 8
                                color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.92)
                                border.width: 1
                                border.color: entry.activeFocus ? frontend.accentColor
                                    : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.12)
                            }
                            onEditingFinished: {
                                if (row.controlState.kind === "number") {
                                    var n = Number(text)
                                    if (text.trim().length && isFinite(n))
                                        frontend.panelModel.edit(row.index, Math.max(row.controlState.minimum, Math.min(row.controlState.maximum, n)))
                                } else {
                                    frontend.panelModel.edit(row.index, text)
                                }
                                text = displayValue
                            }
                        }
                    }
                }

                Component {
                    id: sliderControl
                    Column {
                        width: row.width
                        spacing: 3
                        Text {
                            text: row.controlState.text
                            visible: text.length > 0
                            color: frontend.mutedColor
                            font.pixelSize: 10
                            font.weight: Font.Medium
                        }
                        Slider {
                            width: parent.width
                            from: row.controlState.minimum
                            to: row.controlState.maximum
                            stepSize: row.controlState.step
                            value: row.controlState.value
                            enabled: row.controlState.enabled
                            palette.highlight: frontend.accentColor
                            onMoved: frontend.panelModel.edit(row.index, value)
                            wheelEnabled: false
                        }
                    }
                }

                Component {
                    id: outputControl
                    TextArea {
                        width: row.width
                        implicitHeight: frontend.panelTitle === "Output / Console" ? 430 : 190
                        text: row.controlState.value
                        readOnly: true
                        selectByMouse: true
                        color: frontend.textColor
                        font.family: "Consolas"
                        font.pixelSize: 11
                        wrapMode: TextEdit.NoWrap
                        background: Rectangle {
                            color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.96)
                            radius: 9
                            border.width: 1
                            border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.10)
                        }
                    }
                }
            }
        }

        Item {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 34
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                text: "LIVE · linked to the active Shader Studio backend"
                color: frontend.mutedColor
                opacity: 0.68
                font.pixelSize: 9
                font.letterSpacing: 0.5
            }
        }
    }
}
