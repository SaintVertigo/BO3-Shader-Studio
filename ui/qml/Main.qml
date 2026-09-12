import QtQuick 6.8
import QtQuick.Window 6.8

Window {
    id: root
    width: 1600
    height: 900
    minimumWidth: 960
    minimumHeight: 600
    visible: false
    title: "BO3 Shader Studio " + frontend.displayVersion
    color: frontend.windowColor
    property bool closeApprovedByBackend: false
    property int motion: frontend.animationsEnabled ? 1 : 0
    property bool advancedMounted: !frontend.beginnerMode
    property bool fullPreview: false
    property bool meshMenuOpen: false
    property bool lightingMenuOpen: false
    property bool viewMenuOpen: false
    property bool settingsMenuOpen: false
    // Exposed for the C++ frontend smoke test so it verifies that a View action
    // opens an actual QML utility window, not merely that the reflected model loads.
    readonly property bool utilityPanelVisible: utilityPanel.visible
    property var viewMenuAnchor: null
    property var settingsMenuAnchor: null

    // Keep an independent user-resizable split for Beginner and Advanced.
    // The old QWidget dock layout exposed a draggable preview boundary; the
    // pure-QML shell must preserve that capability instead of hard-coding the
    // authoring width. Ratios make the chosen split scale with window size.
    property real beginnerSplitRatio: 0.235
    property real advancedSplitRatio: 0.36
    property real splitDragStartWidth: 0
    property real splitDragStartBodyX: 0
    property real pendingSplitWidth: 0
    property bool observedBeginnerMode: frontend.beginnerMode
    property int observedTarget: frontend.target

    readonly property real activeSplitRatio: frontend.beginnerMode ? beginnerSplitRatio : advancedSplitRatio
    readonly property real minimumLeftWidth: frontend.beginnerMode ? 280 : 340
    readonly property real minimumPreviewWidth: 360
    property real normalLeftWidth: {
        var desired = width * activeSplitRatio
        var maximum = Math.max(minimumLeftWidth, width - minimumPreviewWidth - 36)
        return Math.max(minimumLeftWidth, Math.min(maximum, desired))
    }
    property real leftTargetWidth: fullPreview ? 0 : normalLeftWidth

    function clampSplitWidth(desiredWidth) {
        var maximum = Math.max(minimumLeftWidth, body.width - minimumPreviewWidth - 36)
        return Math.max(minimumLeftWidth, Math.min(maximum, desiredWidth))
    }

    function setActiveSplitWidth(desiredWidth) {
        var clamped = clampSplitWidth(desiredWidth)
        var ratio = clamped / Math.max(1, root.width)
        if (frontend.beginnerMode) root.beginnerSplitRatio = ratio
        else root.advancedSplitRatio = ratio
    }

    function resetActiveSplit() {
        if (frontend.beginnerMode) root.beginnerSplitRatio = 0.235
        else root.advancedSplitRatio = 0.36
    }

    onClosing: function(close) {
        if (!closeApprovedByBackend) {
            close.accepted = false
            frontend.requestClose()
        }
    }

    function openEffects() { effectBrowser.openBrowser() }

    function positionViewMenu(anchorItem) {
        if (!anchorItem) return
        var p = anchorItem.mapToGlobal(Qt.point(-8, anchorItem.height + 8))
        var g = viewMenuPopup.screen ? viewMenuPopup.screen.availableGeometry : Qt.rect(0, 0, 1920, 1080)
        viewMenuPopup.x = Math.max(g.x + 8, Math.min(g.x + g.width - viewMenuPopup.width - 8, Math.round(p.x)))
        viewMenuPopup.y = Math.max(g.y + 8, Math.min(g.y + g.height - viewMenuPopup.height - 8, Math.round(p.y)))
    }

    function toggleViewMenu(anchorItem) {
        root.viewMenuAnchor = anchorItem
        root.settingsMenuOpen = false
        if (root.viewMenuOpen) {
            root.viewMenuOpen = false
            return
        }
        root.positionViewMenu(anchorItem)
        root.viewMenuOpen = true
    }

    function positionSettingsMenu(anchorItem) {
        if (!anchorItem) return
        var p = anchorItem.mapToGlobal(Qt.point(-8, anchorItem.height + 8))
        var g = settingsMenuPopup.screen ? settingsMenuPopup.screen.availableGeometry : Qt.rect(0, 0, 1920, 1080)
        settingsMenuPopup.x = Math.max(g.x + 8, Math.min(g.x + g.width - settingsMenuPopup.width - 8, Math.round(p.x)))
        settingsMenuPopup.y = Math.max(g.y + 8, Math.min(g.y + g.height - settingsMenuPopup.height - 8, Math.round(p.y)))
    }

    function toggleSettingsMenu(anchorItem) {
        root.settingsMenuAnchor = anchorItem
        root.viewMenuOpen = false
        if (root.settingsMenuOpen) {
            root.settingsMenuOpen = false
            return
        }
        root.positionSettingsMenu(anchorItem)
        root.settingsMenuOpen = true
    }

    function positionMeshPopover() {
        var p = meshButton.mapToGlobal(Qt.point(0, meshButton.height + 7))
        var g = meshPopover.screen ? meshPopover.screen.availableGeometry : Qt.rect(0, 0, 1920, 1080)
        meshPopover.x = Math.max(g.x + 8, Math.min(g.x + g.width - meshPopover.width - 8, Math.round(p.x)))
        meshPopover.y = Math.max(g.y + 8, Math.min(g.y + g.height - meshPopover.height - 8, Math.round(p.y)))
    }

    function positionLightingPopover() {
        var p = lightingButton.mapToGlobal(Qt.point(0, lightingButton.height + 7))
        var g = lightingPopover.screen ? lightingPopover.screen.availableGeometry : Qt.rect(0, 0, 1920, 1080)
        lightingPopover.x = Math.max(g.x + 8, Math.min(g.x + g.width - lightingPopover.width - 8, Math.round(p.x)))
        lightingPopover.y = Math.max(g.y + 8, Math.min(g.y + g.height - lightingPopover.height - 8, Math.round(p.y)))
    }

    onXChanged: {
        if (viewMenuPopup.visible) Qt.callLater(function() { root.positionViewMenu(viewMenuAnchor) })
        if (settingsMenuPopup.visible) Qt.callLater(function() { root.positionSettingsMenu(settingsMenuAnchor) })
        if (meshPopover.visible) Qt.callLater(positionMeshPopover)
        if (lightingPopover.visible) Qt.callLater(positionLightingPopover)
    }
    onYChanged: {
        if (viewMenuPopup.visible) Qt.callLater(function() { root.positionViewMenu(viewMenuAnchor) })
        if (settingsMenuPopup.visible) Qt.callLater(function() { root.positionSettingsMenu(settingsMenuAnchor) })
        if (meshPopover.visible) Qt.callLater(positionMeshPopover)
        if (lightingPopover.visible) Qt.callLater(positionLightingPopover)
    }
    onWidthChanged: {
        if (viewMenuPopup.visible) Qt.callLater(function() { root.positionViewMenu(viewMenuAnchor) })
        if (settingsMenuPopup.visible) Qt.callLater(function() { root.positionSettingsMenu(settingsMenuAnchor) })
        if (meshPopover.visible) Qt.callLater(positionMeshPopover)
        if (lightingPopover.visible) Qt.callLater(positionLightingPopover)
    }
    onHeightChanged: {
        if (viewMenuPopup.visible) Qt.callLater(function() { root.positionViewMenu(viewMenuAnchor) })
        if (settingsMenuPopup.visible) Qt.callLater(function() { root.positionSettingsMenu(settingsMenuAnchor) })
        if (meshPopover.visible) Qt.callLater(positionMeshPopover)
        if (lightingPopover.visible) Qt.callLater(positionLightingPopover)
    }

    Connections {
        target: frontend
        function onStateChanged() {
            // stateChanged also fires for normal project/preview updates. Only
            // run the mode cue when the actual Beginner/Advanced mode changed.
            if (root.observedBeginnerMode !== frontend.beginnerMode) {
                root.observedBeginnerMode = frontend.beginnerMode
                if (frontend.animationsEnabled) modeFlash.restart()
            }
        }
        function onProjectChanged() {
            if (root.observedTarget !== frontend.target) {
                root.observedTarget = frontend.target
                root.meshMenuOpen = false
                root.lightingMenuOpen = false
            }

            // Native WindowContainer children must switch atomically. Animating
            // or delaying their containing geometry can leave Qt Quick with an
            // empty/cull-stale rail until the next mode change. Decorative QML
            // controls still animate; native/structural workspace swaps do not.
        }
        function onGettingStartedRequested() { tutorial.openTour() }
        function onEffectBrowserOpenRequested() { effectBrowser.openBrowser() }
        function onPreviewSettingsOpenRequested() {
            frontend.requestPanel("Preview Settings")
        }
        function onFullPreviewToggleRequested() { root.fullPreview = !root.fullPreview }
        function onCloseApproved() {
            root.closeApprovedByBackend = true
            root.close()
            Qt.quit()
        }
    }

    SequentialAnimation {
        id: modeFlash
        NumberAnimation { target: modePulse; property: "opacity"; from: 0; to: 0.22; duration: 90; easing.type: Easing.OutCubic }
        NumberAnimation { target: modePulse; property: "opacity"; to: 0; duration: 360; easing.type: Easing.OutQuint }
    }

    Rectangle {
        anchors.fill: parent
        color: frontend.windowColor
        gradient: Gradient {
            GradientStop {
                position: 0
                color: frontend.themeName === "Liquid Glass"
                       ? Qt.rgba(0.035, 0.12, 0.19, 1)
                       : Qt.rgba(frontend.panelColor.r * 0.78, frontend.panelColor.g * 0.78, frontend.panelColor.b * 0.78, 1)
            }
            GradientStop { position: 0.62; color: frontend.windowColor }
            GradientStop {
                position: 1
                color: frontend.themeName === "Liquid Glass"
                       ? Qt.rgba(0.04, 0.055, 0.105, 1)
                       : Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 1)
            }
        }
    }

    // Liquid Glass uses restrained chromatic glints rather than large colored
    // washes. The surfaces carry the glass treatment; the workspace background
    // only provides a little color for those translucent edges to pick up.
    Item {
        anchors.fill: parent
        visible: frontend.themeName === "Liquid Glass"
        enabled: false
        Rectangle {
            width: 300; height: 180; radius: 90
            x: root.width - width * 0.62; y: 72
            color: Qt.rgba(0.18, 0.78, 0.96, 0.045)
        }
        Rectangle {
            width: 240; height: 160; radius: 80
            x: -90; y: root.height - height * 0.72
            color: Qt.rgba(0.78, 0.28, 0.72, 0.025)
        }
        Rectangle {
            width: 180; height: 120; radius: 60
            x: root.width * 0.52; y: root.height - 98
            color: Qt.rgba(1.0, 0.42, 0.26, 0.018)
        }
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // custom menu strip
        Item {
            width: parent.width; height: 28
            opacity: 1
            Row {
                anchors.left: parent.left; anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 18
                Repeater {
                    model: ["File", "View", "Theme", "Settings", "Tools", "Help"]
                    delegate: Text {
                        required property string modelData
                        text: modelData
                        color: menuMouse.containsMouse ? frontend.textColor : frontend.mutedColor
                        font.pixelSize: 10
                        font.weight: Font.Medium
                        MouseArea {
                            id: menuMouse
                            anchors.fill: parent
                            anchors.margins: -6
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: frontend.requestMenu(modelData)
                        }
                    }
                }
            }
        }

        // primary command bar
        Item {
            width: parent.width; height: 66
            opacity: 1
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.07) }

            Row {
                anchors.left: parent.left; anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 13
                Row {
                    spacing: 9
                    Rectangle { width: 28; height: 28; radius: 7; color: Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.18); border.width: 1; border.color: Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.5); Text { anchors.centerIn: parent; text: "III"; color: "#ff7a26"; font.bold: true; font.pixelSize: 13 } }
                    Column { anchors.verticalCenter: parent.verticalCenter; spacing: 1; Text { text: "BO3 Shader Studio " + frontend.displayVersion; color: frontend.textColor; font.pixelSize: 14; font.bold: true } Text { text: frontend.projectName.length ? frontend.projectName : frontend.themeName; color: frontend.mutedColor; font.pixelSize: 9; elide: Text.ElideRight; width: 150 } }
                }
                Item { width: 10; height: 1 }
                GlassButton { compact: true; width: 70; height: 32; text: "Open"; onClicked: frontend.requestOpen() }
                GlassButton { compact: true; width: 70; height: 32; text: "Save"; onClicked: frontend.requestSave() }
            }

            Rectangle {
                id: modeSwitch
                width: 196; height: 36; radius: 11
                x: Math.max(430, (parent.width - width) / 2)
                anchors.verticalCenter: parent.verticalCenter
                color: frontend.themeName === "Liquid Glass"
                       ? Qt.rgba(frontend.panelColor.r, frontend.panelColor.g, frontend.panelColor.b, 0.78)
                       : Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.78)
                border.width: 1
                border.color: frontend.themeName === "Liquid Glass"
                              ? Qt.rgba(0.94, 0.99, 1.0, 0.24)
                              : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.09)
                Rectangle {
                    id: modePill
                    width: 95; height: 30; radius: 9; y: 3
                    x: frontend.beginnerMode ? 3 : modeSwitch.width - width - 3
                    color: Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.50)
                    border.width: 1; border.color: frontend.accentColor
                    Behavior on x { NumberAnimation { duration: frontend.animationsEnabled ? 430 : 0; easing.type: Easing.OutQuint } }
                    Behavior on color { ColorAnimation { duration: frontend.animationsEnabled ? 220 : 0; easing.type: Easing.OutCubic } }
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
                width: 132; height: 36; primary: true; text: "Export to BO3"; glyph: "↗"
                onClicked: frontend.requestExport()
            }
        }

        Item {
            id: body
            width: parent.width; height: parent.height - 116

            Item {
                id: leftArea
                x: 12; y: 12
                width: root.leftTargetWidth
                height: body.height - 36
                // Keep native workspace surfaces out of the startup fade as
                // well. WindowContainer content does not participate in QML
                // opacity composition like ordinary scenegraph items.
                opacity: root.fullPreview ? 0 : 1
                visible: !root.fullPreview
                // Do not animate workspace geometry that contains native
                // WindowContainers. Qt/Windows can momentarily desynchronize the
                // native child from the QML scene during those geometry tweens.

                BeginnerRail {
                    id: beginnerRail
                    anchors.fill: parent

                    // Keep the QML rail mounted across mode switches. Tying
                    // visible directly to its animated opacity can leave the
                    // item culled after an Advanced -> Beginner transition on
                    // Windows/Qt Quick, producing the empty left column seen in
                    // the mode-switch regression video.
                    visible: true
                    enabled: frontend.beginnerMode && !root.fullPreview
                    opacity: frontend.beginnerMode ? 1 : 0
                    openEffectBrowser: root.openEffects
                }

                GlassSurface {
                    anchors.fill: parent

                    // Keep the shell mounted, but switch it atomically. The
                    // native editor WindowContainer below is mounted strictly
                    // through advancedMounted, so no stale native surface can
                    // survive a return to Beginner mode.
                    visible: true
                    enabled: !frontend.beginnerMode
                    opacity: frontend.beginnerMode ? 0 : 1
                    glassOpacity: 0.92
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 8
                        radius: 12
                        color: frontend.baseColor
                        border.width: 1
                        border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.08)
                    }
                    WindowContainer {
                        id: advancedSlot
                        objectName: "advancedSlot"
                        anchors.fill: parent
                        anchors.margins: 12
                        visible: root.advancedMounted && frontend.advancedEditorWindow !== null
                        window: frontend.advancedEditorWindow
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

            }

            Item {
                id: splitHandle
                z: 45
                x: leftArea.x + leftArea.width
                y: leftArea.y
                width: 12
                height: leftArea.height
                visible: !root.fullPreview

                MouseArea {
                    id: splitMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: Qt.SizeHorCursor

                    onPressed: function(mouse) {
                        var p = splitHandle.mapToItem(body, mouse.x, mouse.y)
                        root.splitDragStartBodyX = p.x
                        root.splitDragStartWidth = leftArea.width
                        root.pendingSplitWidth = leftArea.width
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        // Track the pointer in body coordinates and update the real
                        // split immediately. The native frame pump synchronizes
                        // the swapchain before rendering at the current size.
                        var p = splitHandle.mapToItem(body, mouse.x, mouse.y)
                        root.pendingSplitWidth = root.clampSplitWidth(root.splitDragStartWidth + p.x - root.splitDragStartBodyX)
                        root.setActiveSplitWidth(root.pendingSplitWidth)
                    }
                    onReleased: root.setActiveSplitWidth(root.pendingSplitWidth)
                    onCanceled: root.pendingSplitWidth = leftArea.width
                    onDoubleClicked: root.resetActiveSplit()
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    width: splitMouse.pressed ? 3 : 2
                    height: splitMouse.containsMouse || splitMouse.pressed ? Math.min(96, parent.height * 0.22) : 34
                    radius: width / 2
                    color: frontend.accentColor
                    opacity: splitMouse.pressed ? 0.90 : (splitMouse.containsMouse ? 0.58 : 0.16)
                    Behavior on opacity { NumberAnimation { duration: frontend.animationsEnabled ? 110 : 0 } }
                    Behavior on height { NumberAnimation { duration: frontend.animationsEnabled ? 150 : 0; easing.type: Easing.OutCubic } }
                }
            }

            Item {
                id: previewArea
                x: leftArea.x + leftArea.width + 12
                y: 12
                width: body.width - x - 12
                height: body.height - 36
                // Preview geometry changes are intentionally immediate. The
                // D3D WindowContainer follows a draggable split and must never
                // be tweened independently of its native child window.

                GlassSurface { anchors.fill: parent; glassOpacity: 0.86; cornerRadius: 19 }

                Rectangle {
                    id: previewHud
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: 10
                    z: 30
                    height: previewCommands.implicitHeight + 16; radius: 12
                    color: frontend.themeName === "Liquid Glass"
                           ? Qt.rgba(frontend.panelColor.r, frontend.panelColor.g, frontend.panelColor.b, 0.84)
                           : Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.82)
                    border.width: 1
                    border.color: frontend.themeName === "Liquid Glass"
                                  ? Qt.rgba(0.94, 0.99, 1.0, 0.25)
                                  : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.11)
                    Flow {
                        id: previewCommands
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.margins: 8
                        y: 8
                        spacing: 6
                        GlassButton {
                            compact: true; width: 94; height: 30
                            text: frontend.target === 0 ? "2D Preview" : (frontend.previewCamera3D ? "Perspective" : "2D View")
                            checked: frontend.target !== 0 && frontend.previewCamera3D
                            enabled: frontend.target !== 0
                            onClicked: frontend.requestCamera3D(!frontend.previewCamera3D)
                        }
                        GlassButton {
                            id: meshButton
                            visible: frontend.target === 1
                            compact: true; width: 102; height: 30
                            text: frontend.previewMeshName + "  ▾"
                            onClicked: {
                                root.lightingMenuOpen = false
                                root.meshMenuOpen = !root.meshMenuOpen
                                if (root.meshMenuOpen) Qt.callLater(root.positionMeshPopover)
                            }
                        }
                        GlassButton {
                            id: lightingButton
                            visible: frontend.target === 1
                            compact: true; width: 132; height: 30
                            text: "APE Lighting · " + frontend.apeLightingName
                            onClicked: {
                                root.meshMenuOpen = false
                                root.lightingMenuOpen = !root.lightingMenuOpen
                                if (root.lightingMenuOpen) Qt.callLater(root.positionLightingPopover)
                            }
                        }
                        GlassButton { compact: true; width: 66; height: 30; text: "Reset"; onClicked: frontend.requestResetPreview() }
                        GlassButton { compact: true; width: 104; height: 30; text: root.fullPreview ? "Restore" : "Full Preview"; checked: root.fullPreview; onClicked: root.fullPreview = !root.fullPreview }
                    GlassButton {
                        compact: true; width: 82; height: 30; text: "Settings"
                        onClicked: { frontend.requestPanel("Preview Settings"); root.meshMenuOpen = false; root.lightingMenuOpen = false }
                    }
                    }
                }

                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: previewHud.bottom
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 10; anchors.rightMargin: 10; anchors.topMargin: 8; anchors.bottomMargin: 10
                    radius: 16
                    color: frontend.themeName === "Liquid Glass"
                           ? Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.93)
                           : frontend.baseColor
                    border.width: 1
                    border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.09)
                    clip: true
                    WindowContainer {
                        id: previewSlot
                        objectName: "previewSlot"
                        anchors.fill: parent
                        anchors.margins: 7
                        window: frontend.previewWindow
                        visible: frontend.previewWindow !== null
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 7
                        visible: frontend.previewWindow === null
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "PREVIEW"; color: frontend.accentColor; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.0 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Initializing Direct3D preview…"; color: frontend.mutedColor; font.pixelSize: 12 }
                    }
                    Rectangle { anchors.fill: parent; color: "transparent"; border.width: 1; border.color: Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.10); radius: 12; visible: frontend.previewWindow === null }
                }
            }

            Rectangle {
                id: modePulse
                z: 50
                x: leftArea.x + leftArea.width - width / 2
                y: 20
                width: 8
                height: Math.max(0, leftArea.height - 16)
                radius: 4
                color: frontend.accentColor
                opacity: 0

                // The old 112px capsule swept across the entire workspace and
                // looked like a large blue panel during every mode change.
                // Retain the transition cue as a narrow boundary highlight.
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

    Window {
        id: viewMenuPopup
        width: 248
        height: 404
        visible: root.viewMenuOpen
        transientParent: root
        flags: Qt.Tool | Qt.FramelessWindowHint
        color: "transparent"
        title: "View"

        onVisibleChanged: {
            if (visible) {
                Qt.callLater(function() {
                    root.positionViewMenu(root.viewMenuAnchor)
                    viewMenuPopup.raise()
                    viewMenuPopup.requestActivate()
                })
            }
        }

        GlassSurface {
            anchors.fill: parent
            cornerRadius: 12
            glassOpacity: 0.98

            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 3

                Repeater {
                    model: [
                        "Output / Console", "Inputs", "Parameters", "Material Textures",
                        "Performance", "Scene / Lighting", "Script Vectors", "Preview Settings"
                    ]
                    delegate: GlassButton {
                        required property string modelData
                        width: 232
                        height: 31
                        compact: true
                        text: modelData
                        onClicked: {
                            root.viewMenuOpen = false
                            frontend.requestPanel(modelData)
                        }
                    }
                }

                Rectangle {
                    width: 232
                    height: 1
                    color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.10)
                }

                GlassButton {
                    width: 232; height: 31; compact: true; text: "Full Preview"
                    onClicked: { root.viewMenuOpen = false; frontend.requestFullPreview() }
                }
                GlassButton {
                    width: 232; height: 31; compact: true; text: "Add Effect Browser"
                    onClicked: { root.viewMenuOpen = false; frontend.requestBrowseEffects() }
                }
                GlassButton {
                    width: 232; height: 31; compact: true; text: "Reset Preview"
                    onClicked: { root.viewMenuOpen = false; frontend.requestResetPreview() }
                }
            }

            focus: true
            Keys.onEscapePressed: root.viewMenuOpen = false
        }
    }

    Window {
        id: settingsMenuPopup
        width: 264
        height: 294
        visible: root.settingsMenuOpen
        transientParent: root
        flags: Qt.Tool | Qt.FramelessWindowHint
        color: "transparent"
        title: "Settings"

        onVisibleChanged: {
            if (visible) {
                Qt.callLater(function() {
                    root.positionSettingsMenu(root.settingsMenuAnchor)
                    settingsMenuPopup.raise()
                    settingsMenuPopup.requestActivate()
                })
            }
        }

        GlassSurface {
            anchors.fill: parent
            cornerRadius: 12
            glassOpacity: 0.98

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 7

                Text {
                    text: "INTERFACE MODE"
                    color: frontend.mutedColor
                    font.pixelSize: 9
                    font.bold: true
                    font.letterSpacing: 0.8
                }

                Row {
                    spacing: 6
                    GlassButton {
                        width: 119; height: 32; compact: true
                        text: "Beginner"
                        checked: frontend.beginnerMode
                        onClicked: frontend.requestMode(true)
                    }
                    GlassButton {
                        width: 119; height: 32; compact: true
                        text: "Advanced"
                        checked: !frontend.beginnerMode
                        onClicked: frontend.requestMode(false)
                    }
                }

                Rectangle {
                    width: 244; height: 1
                    color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.10)
                }

                GlassButton {
                    width: 244; height: 32; compact: true
                    text: frontend.animationsEnabled ? "Animations  ·  On" : "Animations  ·  Off"
                    checked: frontend.animationsEnabled
                    onClicked: frontend.requestAnimationsEnabled(!frontend.animationsEnabled)
                }
                GlassButton {
                    width: 244; height: 32; compact: true
                    text: "Accent Color…"
                    onClicked: {
                        root.settingsMenuOpen = false
                        frontend.requestAccentColor()
                    }
                }
                GlassButton {
                    width: 244; height: 32; compact: true
                    text: "Use Theme Accent"
                    onClicked: {
                        root.settingsMenuOpen = false
                        frontend.requestResetAccent()
                    }
                }

                Rectangle {
                    width: 244; height: 1
                    color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.10)
                }

                GlassButton {
                    width: 244; height: 32; compact: true
                    text: "Keybinds…"
                    onClicked: {
                        root.settingsMenuOpen = false
                        frontend.requestKeybinds()
                    }
                }
            }

            focus: true
            Keys.onEscapePressed: root.settingsMenuOpen = false
        }
    }

    Window {
        id: meshPopover
        width: 306
        height: 112
        visible: root.meshMenuOpen && frontend.target === 1
        transientParent: root
        flags: Qt.Tool | Qt.FramelessWindowHint
        color: "transparent"
        title: "Preview Object"
        onVisibleChanged: {
            if (visible) {
                Qt.callLater(root.positionMeshPopover)
                requestActivate()
            }
        }
        onActiveChanged: if (visible && !active) Qt.callLater(function() {
            if (meshPopover.visible && !meshPopover.active) root.meshMenuOpen = false
        })

        GlassSurface {
            anchors.fill: parent
            cornerRadius: 13
            glassOpacity: 0.96
            Rectangle {
                visible: frontend.themeName !== "Liquid Glass"
                anchors.fill: parent
                anchors.margins: 1
                radius: 12
                color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.82)
            }
            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 7
                Text {
                    text: "PREVIEW OBJECT"
                    color: frontend.mutedColor
                    font.pixelSize: 9
                    font.bold: true
                    font.letterSpacing: 0.8
                }
                Grid {
                    columns: 3
                    columnSpacing: 6
                    rowSpacing: 6
                    Repeater {
                        model: ["Sphere", "Cube", "Plane", "Cylinder", "Monkey", "Card"]
                        delegate: GlassButton {
                            required property string modelData
                            required property int index
                            compact: true
                            width: 91
                            height: 30
                            text: modelData
                            checked: frontend.previewMeshIndex === index
                            onClicked: frontend.requestMesh(index)
                        }
                    }
                }
            }
            focus: true
            Keys.onEscapePressed: root.meshMenuOpen = false
        }
    }

    Window {
        id: lightingPopover
        width: 356
        height: 76
        visible: root.lightingMenuOpen && frontend.target === 1
        transientParent: root
        flags: Qt.Tool | Qt.FramelessWindowHint
        color: "transparent"
        title: "APE Lighting"
        onVisibleChanged: {
            if (visible) {
                Qt.callLater(root.positionLightingPopover)
                requestActivate()
            }
        }
        onActiveChanged: if (visible && !active) Qt.callLater(function() {
            if (lightingPopover.visible && !lightingPopover.active) root.lightingMenuOpen = false
        })

        GlassSurface {
            anchors.fill: parent
            cornerRadius: 13
            glassOpacity: 0.96
            Row {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6
                Repeater {
                    model: ["Morning", "Day", "Sunset", "Night"]
                    delegate: GlassButton {
                        required property string modelData
                        required property int index
                        compact: true
                        width: 79
                        height: 32
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        checked: frontend.apeLightingIndex === index
                        onClicked: frontend.requestApeLighting(index)
                    }
                }
            }
            focus: true
            Keys.onEscapePressed: root.lightingMenuOpen = false
        }
    }

    UtilityPanel { id: utilityPanel; transientParent: root }
    EffectBrowser { id: effectBrowser; transientParent: root }
    TutorialOverlay { id: tutorial; transientParent: root; leftWidth: root.leftTargetWidth }
}
