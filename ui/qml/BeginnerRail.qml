import QtQuick 6.8

GlassSurface {
    id: rail
    property var openEffectBrowser
    property bool presetOpen: false
    cornerRadius: 19
    glassOpacity: 0.91

    function currentPresetName() {
        for (var i = 0; i < frontend.presets.length; ++i)
            if (frontend.presets[i].id === frontend.selectedPresetId) return frontend.presets[i].name
        return frontend.presets.length ? frontend.presets[0].name : "Blank"
    }
    function currentPresetDescription() {
        for (var i = 0; i < frontend.presets.length; ++i)
            if (frontend.presets[i].id === frontend.selectedPresetId) return frontend.presets[i].description
        return "Choose a clean starting point for this shader."
    }
    function baseColor(key, fallback) {
        var v = frontend.baseSettings ? frontend.baseSettings[key] : undefined
        return v ? v : fallback
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 14
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 1900

        Column {
            id: content
            width: flick.width
            spacing: 17

            Column {
                width: parent.width
                spacing: 4
                Text { text: "BEGINNER"; color: frontend.textColor; font.pixelSize: 18; font.bold: true; font.letterSpacing: 0.5 }
                Text {
                    width: parent.width
                    text: "Build BO3-compatible shaders visually with effects, sliders, and live preview."
                    color: frontend.mutedColor
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    lineHeight: 1.18
                }
            }

            Column {
                width: parent.width
                spacing: 9
                Text { text: "1  SHADER TYPE"; color: frontend.mutedColor; font.pixelSize: 10; font.bold: true; font.letterSpacing: 0.9 }
                Row {
                    width: parent.width
                    spacing: 7
                    Repeater {
                        model: [
                            {label:"Screen", sub:"Effect", glyph:"▣"},
                            {label:"Material", sub:"Surface", glyph:"●"},
                            {label:"Sky", sub:"Environment", glyph:"▲"}
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: (parent.width - 14) / 3
                            height: 72
                            radius: 12
                            color: frontend.target === index
                                   ? Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.38)
                                   : targetMouse.containsMouse
                                     ? Qt.rgba(frontend.buttonColor.r, frontend.buttonColor.g, frontend.buttonColor.b, 0.68)
                                     : Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.55)
                            border.width: frontend.target === index ? 1.3 : 1
                            border.color: frontend.target === index
                                          ? frontend.accentColor
                                          : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.08)
                            scale: targetMouse.pressed ? 0.97 : targetMouse.containsMouse ? 1.015 : 1
                            Behavior on scale { NumberAnimation { duration: frontend.animationsEnabled ? 110 : 0; easing.type: Easing.OutCubic } }
                            Behavior on color { ColorAnimation { duration: frontend.animationsEnabled ? 135 : 0 } }
                            Column {
                                anchors.centerIn: parent
                                spacing: 2
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.glyph; color: frontend.target === index ? "white" : frontend.accentColor; font.pixelSize: 17 }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.label; color: frontend.textColor; font.pixelSize: 11; font.bold: true }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.sub; color: frontend.mutedColor; font.pixelSize: 9 }
                            }
                            MouseArea { id: targetMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: frontend.requestTarget(index) }
                        }
                    }
                }
                Text { width: parent.width; text: frontend.targetDescription; color: frontend.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
            }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.065) }

            Column {
                width: parent.width
                spacing: 9
                Text { text: "2  PROJECT"; color: frontend.mutedColor; font.pixelSize: 10; font.bold: true; font.letterSpacing: 0.9 }

                Text { text: "Name"; color: frontend.mutedColor; font.pixelSize: 9 }
                Rectangle {
                    width: parent.width
                    height: 40
                    radius: 11
                    color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.54)
                    border.width: 1
                    border.color: projectName.activeFocus ? frontend.accentColor : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.08)
                    TextInput {
                        id: projectName
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter
                        text: frontend.projectName
                        color: frontend.textColor
                        font.pixelSize: 12
                        selectByMouse: true
                        onEditingFinished: frontend.requestProjectName(text)
                    }
                }

                Text { text: "Starting Point"; color: frontend.mutedColor; font.pixelSize: 9 }
                Rectangle {
                    id: presetButton
                    width: parent.width
                    height: 40
                    radius: 11
                    color: presetMouse.containsMouse
                           ? Qt.rgba(frontend.buttonColor.r, frontend.buttonColor.g, frontend.buttonColor.b, 0.74)
                           : Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.54)
                    border.width: 1
                    border.color: rail.presetOpen ? frontend.accentColor : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.08)
                    Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: rail.currentPresetName(); color: frontend.textColor; font.pixelSize: 11; font.weight: Font.Medium }
                    Text { anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: rail.presetOpen ? "⌃" : "⌄"; color: frontend.mutedColor; font.pixelSize: 13 }
                    MouseArea { id: presetMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: rail.presetOpen = !rail.presetOpen }
                }

                Column {
                    width: parent.width
                    spacing: 5
                    visible: rail.presetOpen
                    opacity: rail.presetOpen ? 1 : 0
                    Repeater {
                        model: frontend.presets
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width
                            height: 42
                            radius: 10
                            color: presetChoiceMouse.containsMouse
                                   ? Qt.rgba(frontend.buttonColor.r, frontend.buttonColor.g, frontend.buttonColor.b, 0.68)
                                   : Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.40)
                            border.width: modelData.id === frontend.selectedPresetId ? 1 : 0
                            border.color: frontend.accentColor
                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 11
                                anchors.right: parent.right
                                anchors.rightMargin: 9
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 1
                                Text { text: modelData.name; color: frontend.textColor; font.pixelSize: 10; font.bold: modelData.id === frontend.selectedPresetId }
                                Text { width: parent.width; text: modelData.description; color: frontend.mutedColor; font.pixelSize: 8; elide: Text.ElideRight }
                            }
                            MouseArea {
                                id: presetChoiceMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    frontend.requestApplyPreset(modelData.id)
                                    rail.presetOpen = false
                                }
                            }
                        }
                    }
                }
                Text { visible: !rail.presetOpen; width: parent.width; text: rail.currentPresetDescription(); color: frontend.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
            }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.065) }

            Column {
                width: parent.width
                spacing: 8
                Text {
                    text: frontend.target === 0 ? "STARTING IMAGE" : frontend.target === 1 ? "BASE SURFACE" : "SKY PALETTE"
                    color: frontend.mutedColor
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 0.9
                }
                Text {
                    visible: frontend.target === 0
                    width: parent.width
                    text: "The game scene is the starting image. Add effects below to shape it."
                    color: frontend.mutedColor
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
                ColorField { visible: frontend.target === 1; width: parent.width; label: "Base Color"; settingKey: "baseColor"; value: rail.baseColor("baseColor", "#2F78D0") }
                Column {
                    visible: frontend.target === 2
                    width: parent.width
                    spacing: 4
                    ColorField { width: parent.width; label: "Top / Zenith"; settingKey: "zenithColor"; value: rail.baseColor("zenithColor", "#102E68") }
                    ColorField { width: parent.width; label: "Horizon"; settingKey: "horizonColor"; value: rail.baseColor("horizonColor", "#E17658") }
                    ColorField { width: parent.width; label: "Bottom / Ground"; settingKey: "groundColor"; value: rail.baseColor("groundColor", "#060B18") }
                }
            }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.065) }

            Column {
                width: parent.width
                spacing: 10
                Row {
                    width: parent.width
                    Text { text: "3  EFFECTS"; color: frontend.mutedColor; font.pixelSize: 10; font.bold: true; font.letterSpacing: 0.9 }
                    Item { width: Math.max(4, parent.width - 115); height: 1 }
                    Text { text: frontend.activeEffects.length + " active"; color: frontend.mutedColor; font.pixelSize: 9 }
                }

                Rectangle {
                    visible: frontend.activeEffects.length === 0
                    width: parent.width
                    height: 116
                    radius: 14
                    color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.42)
                    border.width: 1
                    border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.065)
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - 28
                        spacing: 8
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "No effects yet"; color: frontend.textColor; font.pixelSize: 12; font.bold: true }
                        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: "Build the look one visual module at a time."; color: frontend.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
                        GlassButton { anchors.horizontalCenter: parent.horizontalCenter; width: 190; height: 34; compact: true; primary: true; text: "+ Add Your First Effect"; onClicked: rail.openEffectBrowser() }
                    }
                }

                Column {
                    visible: frontend.activeEffects.length > 0
                    width: parent.width
                    spacing: 7
                    Repeater {
                        model: frontend.activeEffects
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: parent.width
                            height: 54
                            radius: 12
                            color: frontend.selectedEffectIndex === index
                                   ? Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.21)
                                   : effectMouse.containsMouse
                                     ? Qt.rgba(frontend.buttonColor.r, frontend.buttonColor.g, frontend.buttonColor.b, 0.58)
                                     : Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.43)
                            border.width: 1
                            border.color: frontend.selectedEffectIndex === index
                                          ? Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.62)
                                          : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.065)
                            Behavior on color { ColorAnimation { duration: frontend.animationsEnabled ? 115 : 0 } }
                            Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.top: parent.top; anchors.topMargin: 9; text: modelData.name; color: frontend.textColor; font.pixelSize: 11; font.bold: true }
                            Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.bottom: parent.bottom; anchors.bottomMargin: 8; text: modelData.category; color: frontend.mutedColor; font.pixelSize: 9 }
                            Rectangle {
                                id: enabledPill
                                anchors.right: parent.right
                                anchors.rightMargin: 9
                                anchors.verticalCenter: parent.verticalCenter
                                width: 42
                                height: 24
                                radius: 8
                                color: modelData.enabled
                                       ? Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.28)
                                       : Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.72)
                                Text { anchors.centerIn: parent; text: modelData.enabled ? "ON" : "OFF"; color: modelData.enabled ? frontend.accentColor : frontend.mutedColor; font.pixelSize: 8; font.bold: true }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: frontend.requestEffectEnabled(index, !modelData.enabled) }
                            }
                            MouseArea { id: effectMouse; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: enabledPill.left; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: frontend.requestSelectEffect(index) }
                        }
                    }
                    GlassButton { width: parent.width; height: 36; compact: true; text: "+ Add Effect"; onClicked: rail.openEffectBrowser() }
                }
            }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.065) }

            Column {
                width: parent.width
                spacing: 10
                Text { text: "4  SELECTED EFFECT"; color: frontend.mutedColor; font.pixelSize: 10; font.bold: true; font.letterSpacing: 0.9 }

                Column {
                    visible: !frontend.selectedEffect.valid
                    width: parent.width
                    spacing: 5
                    Text { text: "Nothing selected"; color: frontend.textColor; font.pixelSize: 12; font.bold: true }
                    Text { width: parent.width; text: "Select an effect to reveal its focused controls here."; color: frontend.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
                }

                Column {
                    visible: frontend.selectedEffect.valid
                    width: parent.width
                    spacing: 9
                    Row {
                        width: parent.width
                        spacing: 5
                        Column {
                            width: Math.max(80, parent.width - 126)
                            spacing: 2
                            Text { text: frontend.selectedEffect.name || ""; color: frontend.textColor; font.pixelSize: 14; font.bold: true }
                            Text { text: frontend.selectedEffect.category || ""; color: frontend.accentColor; font.pixelSize: 9; font.bold: true }
                        }
                        GlassButton { width: 34; height: 30; compact: true; text: "↑"; onClicked: frontend.requestMoveEffect(frontend.selectedEffectIndex, -1) }
                        GlassButton { width: 34; height: 30; compact: true; text: "↓"; onClicked: frontend.requestMoveEffect(frontend.selectedEffectIndex, 1) }
                        GlassButton { width: 48; height: 30; compact: true; text: "×"; onClicked: frontend.requestRemoveEffect(frontend.selectedEffectIndex) }
                    }
                    Text { width: parent.width; text: frontend.selectedEffect.description || ""; color: frontend.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    Repeater {
                        model: frontend.selectedEffect.parameters || []
                        delegate: ParameterControl {
                            required property var modelData
                            width: parent.width
                            parameterData: modelData
                            effectInstanceId: frontend.selectedEffect.instanceId || ""
                        }
                    }
                }
            }

            Item { width: 1; height: 12 }
        }
    }
}
