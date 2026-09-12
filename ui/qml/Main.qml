import QtQuick 6.8
import QtQuick.Window 6.8

Item {
    id: root
    width: 1600
    height: 900
    focus: true
    property int motion: frontend.animationsEnabled ? 1 : 0
    property bool advancedMounted: !frontend.beginnerMode
    property bool fullPreview: false
    property real normalLeftWidth: frontend.beginnerMode ? Math.max(330, Math.min(390, width * 0.235)) : Math.max(500, Math.min(620, width * 0.36))
    property real leftTargetWidth: fullPreview ? 0 : normalLeftWidth

    function openEffects() { effectBrowser.openBrowser() }

    Connections {
        target: frontend
        function onStateChanged() {
            if (frontend.animationsEnabled) modeFlash.restart()
            if (frontend.beginnerMode) {
                advancedDelay.stop()
                root.advancedMounted = false
            } else if (frontend.animationsEnabled) {
                advancedDelay.restart()
            } else {
                root.advancedMounted = true
            }
        }
        function onGettingStartedRequested() { tutorial.openTour() }
        function onEffectBrowserOpenRequested() { effectBrowser.openBrowser() }
    }

    Timer { id: advancedDelay; interval: 170; repeat: false; onTriggered: root.advancedMounted = true }
    SequentialAnimation {
        id: modeFlash
        NumberAnimation { target: modePulse; property: "opacity"; from: 0; to: 0.20; duration: 110; easing.type: Easing.OutCubic }
        NumberAnimation { target: modePulse; property: "opacity"; to: 0; duration: 430; easing.type: Easing.OutQuint }
    }

    Rectangle {
        anchors.fill: parent
        color: frontend.windowColor
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(frontend.panelColor.r * 0.78, frontend.panelColor.g * 0.78, frontend.panelColor.b * 0.78, 1) }
            GradientStop { position: 0.62; color: frontend.windowColor }
            GradientStop { position: 1; color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 1) }
        }
    }

    // soft accent atmosphere behind glass panels
    Rectangle {
        width: 560; height: 340; radius: 280
        x: root.width * 0.47; y: -210
        color: Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.055)
        rotation: 8
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // custom menu strip
        Item {
            width: parent.width; height: 28
            Row {
                anchors.left: parent.left; anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 18
                Repeater {
                    model: ["File", "View", "Layout", "Theme", "Settings", "Tools", "Help"]
                    delegate: Text {
                        required property string modelData
                        text: modelData
                        color: menuMouse.containsMouse ? frontend.textColor : frontend.mutedColor
                        font.pixelSize: 10
                        font.weight: Font.Medium
                        MouseArea { id: menuMouse; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: frontend.requestMenu(modelData) }
                    }
                }
            }
        }

        // primary command bar
        Item {
            width: parent.width; height: 72
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.07) }

            Row {
                anchors.left: parent.left; anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 15
                Row {
                    spacing: 10
                    Rectangle { width: 30; height: 30; radius: 8; color: Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.18); border.width: 1; border.color: Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.5); Text { anchors.centerIn: parent; text: "III"; color: "#ff7a26"; font.bold: true; font.pixelSize: 13 } }
                    Column { anchors.verticalCenter: parent.verticalCenter; spacing: 1; Text { text: "BO3 Shader Studio " + frontend.displayVersion; color: frontend.textColor; font.pixelSize: 15; font.bold: true } Text { text: frontend.themeName; color: frontend.mutedColor; font.pixelSize: 9; font.letterSpacing: 0.7 } }
                }
                Item { width: 14; height: 1 }
                GlassButton { compact: true; width: 78; text: "Open"; glyph: "▰"; onClicked: frontend.requestOpen() }
                GlassButton { compact: true; width: 78; text: "Save"; glyph: "▣"; onClicked: frontend.requestSave() }
                GlassButton { compact: true; width: 92; text: "Preview"; glyph: "▶"; onClicked: frontend.requestPreview() }
            }

            Rectangle {
                id: modeSwitch
                width: 218; height: 40; radius: 13
                anchors.centerIn: parent
                color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.78)
                border.width: 1
                border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.09)
                Rectangle {
                    id: modePill
                    width: 106; height: 34; radius: 11; y: 3
                    x: frontend.beginnerMode ? 3 : modeSwitch.width - width - 3
                    color: Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.50)
                    border.width: 1; border.color: frontend.accentColor
                    Behavior on x { NumberAnimation { duration: frontend.animationsEnabled ? 380 : 0; easing.type: Easing.OutQuint } }
                }
                Row {
                    anchors.fill: parent
                    Repeater {
                        model: ["Beginner", "Advanced"]
                        delegate: Item {
                            required property string modelData
                            required property int index
                            width: modeSwitch.width / 2; height: modeSwitch.height
                            Text { anchors.centerIn: parent; text: modelData; color: (frontend.beginnerMode === (index === 0)) ? "white" : frontend.mutedColor; font.pixelSize: 12; font.weight: (frontend.beginnerMode === (index === 0)) ? Font.DemiBold : Font.Medium }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: frontend.requestMode(index === 0) }
                        }
                    }
                }
            }

            GlassButton {
                anchors.right: parent.right; anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                width: 142; height: 40; primary: true; text: "Export to BO3"; glyph: "↗"
                onClicked: frontend.requestExport()
            }
        }

        Item {
            id: body
            width: parent.width; height: parent.height - 122

            Item {
                id: leftArea
                x: 12; y: 12
                width: root.leftTargetWidth
                height: body.height - 36
                opacity: root.fullPreview ? 0 : 1
                visible: opacity > 0.01 || !root.fullPreview
                Behavior on width { NumberAnimation { duration: frontend.animationsEnabled ? 520 : 0; easing.type: Easing.OutQuint } }
                Behavior on opacity { NumberAnimation { duration: frontend.animationsEnabled ? 250 : 0; easing.type: Easing.OutCubic } }

                BeginnerRail {
                    id: beginnerRail
                    anchors.fill: parent
                    opacity: frontend.beginnerMode ? 1 : 0
                    x: frontend.beginnerMode ? 0 : -42
                    visible: opacity > 0.01
                    openEffectBrowser: root.openEffects
                    Behavior on opacity { NumberAnimation { duration: frontend.animationsEnabled ? 230 : 0; easing.type: Easing.OutCubic } }
                    Behavior on x { NumberAnimation { duration: frontend.animationsEnabled ? 430 : 0; easing.type: Easing.OutQuint } }
                }

                GlassSurface {
                    anchors.fill: parent
                    visible: !frontend.beginnerMode || root.advancedMounted
                    opacity: frontend.beginnerMode ? 0 : 1
                    glassOpacity: 0.92
                    Behavior on opacity { NumberAnimation { duration: frontend.animationsEnabled ? 230 : 0 } }
                    Item {
                        id: advancedSlot
                        objectName: "advancedSlot"
                        anchors.fill: parent
                        anchors.margins: 8
                        visible: root.advancedMounted
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: !root.advancedMounted
                        color: Qt.rgba(frontend.panelColor.r, frontend.panelColor.g, frontend.panelColor.b, 0.94)
                        radius: 16
                        Column {
                            anchors.centerIn: parent; spacing: 8
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "ADVANCED"; color: frontend.accentColor; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.2 }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Opening shader workspace…"; color: frontend.textColor; font.pixelSize: 13 }
                        }
                    }
                }

                // travelling light seam during mode changes
                Rectangle {
                    width: 2; height: parent.height - 26; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    color: frontend.accentColor
                    opacity: frontend.animationsEnabled ? 0.20 : 0
                    SequentialAnimation on opacity {
                        running: frontend.animationsEnabled
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.45; duration: 1400; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 0.10; duration: 1400; easing.type: Easing.InOutSine }
                    }
                }
            }

            Item {
                id: previewArea
                x: leftArea.x + leftArea.width + 12
                y: 12
                width: body.width - x - 12
                height: body.height - 36
                Behavior on x { NumberAnimation { duration: frontend.animationsEnabled ? 520 : 0; easing.type: Easing.OutQuint } }
                Behavior on width { NumberAnimation { duration: frontend.animationsEnabled ? 520 : 0; easing.type: Easing.OutQuint } }

                GlassSurface { anchors.fill: parent; glassOpacity: 0.86; cornerRadius: 19 }

                Rectangle {
                    id: previewHud
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: 10
                    height: 48; radius: 14
                    color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.76)
                    border.width: 1; border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.10)
                    Row {
                        anchors.left: parent.left; anchors.leftMargin: 9; anchors.verticalCenter: parent.verticalCenter; spacing: 7
                        GlassButton { compact: true; width: 92; height: 32; text: frontend.target === 0 ? "2D Preview" : "Perspective"; checked: frontend.target !== 0; onClicked: frontend.requestCamera3D(frontend.target !== 0) }
                        GlassButton { visible: frontend.target === 1; compact: true; width: 78; height: 32; text: "Sphere"; glyph: "●"; onClicked: frontend.requestMesh(0) }
                        GlassButton { visible: frontend.target === 1; compact: true; width: 96; height: 32; text: "APE Match"; checked: true }
                        GlassButton { compact: true; width: 70; height: 32; text: "Reset"; glyph: "↻"; onClicked: frontend.requestResetPreview() }
                        GlassButton { compact: true; width: 112; height: 32; text: root.fullPreview ? "Restore" : "Full Preview"; glyph: "⛶"; checked: root.fullPreview; onClicked: root.fullPreview = !root.fullPreview }
                    }
                    GlassButton { anchors.right: parent.right; anchors.rightMargin: 9; anchors.verticalCenter: parent.verticalCenter; compact: true; width: 92; height: 32; text: "Settings"; glyph: "⚙"; onClicked: frontend.requestPreviewSettings() }
                }

                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: previewHud.bottom; anchors.bottom: parent.bottom
                    anchors.leftMargin: 10; anchors.rightMargin: 10; anchors.topMargin: 8; anchors.bottomMargin: 10
                    radius: 13
                    color: frontend.baseColor
                    border.width: 1
                    border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.09)
                    clip: true
                    Item {
                        id: previewSlot
                        objectName: "previewSlot"
                        anchors.fill: parent
                        anchors.margins: 1
                    }
                    Rectangle { anchors.fill: parent; color: "transparent"; border.width: 1; border.color: Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.10); radius: 12 }
                }
            }

            Rectangle {
                id: modePulse
                z: 50
                x: leftArea.x + leftArea.width - width / 2
                y: 12
                width: 86
                height: leftArea.height
                radius: 42
                color: frontend.accentColor
                opacity: 0
                Behavior on x { NumberAnimation { duration: frontend.animationsEnabled ? 520 : 0; easing.type: Easing.OutQuint } }
            }

            Text {
                anchors.left: parent.left; anchors.leftMargin: 18; anchors.bottom: parent.bottom; anchors.bottomMargin: 7
                text: frontend.statusText
                color: frontend.mutedColor
                font.pixelSize: 9
                elide: Text.ElideRight
                width: parent.width - 36
            }
        }
    }

    EffectBrowser { id: effectBrowser; transientParent: frontend.hostWindow }
    TutorialOverlay { id: tutorial; transientParent: frontend.hostWindow; leftWidth: root.leftTargetWidth }
}
