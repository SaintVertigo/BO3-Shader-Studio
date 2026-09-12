import QtQuick 6.8
import QtQuick.Window 6.8

Window {
    id: browser
    width: 940
    height: 660
    minimumWidth: 720
    minimumHeight: 520
    flags: Qt.Dialog | Qt.FramelessWindowHint
    modality: Qt.ApplicationModal
    color: "transparent"
    visible: false
    opacity: 0
    title: "Add an Effect"
    x: transientParent ? transientParent.x + Math.max(24, (transientParent.width - width) / 2) : 120
    y: transientParent ? transientParent.y + Math.max(36, (transientParent.height - height) / 2) : 100

    property string searchText: ""
    property string selectedCategory: "All Effects"
    property real contentScale: 0.975
    property real contentLift: 18
    property real chromeGlow: 0
    property var categories: buildCategories()
    property var filteredEffects: filterEffects()

    function buildCategories() {
        var result = ["All Effects"]
        var seen = {}
        for (var i = 0; i < frontend.effectCatalog.length; ++i) {
            var c = frontend.effectCatalog[i].category
            if (c && !seen[c]) { seen[c] = true; result.push(c) }
        }
        return result
    }
    function filterEffects() {
        var result = []
        var q = searchText.toLowerCase().trim()
        for (var i = 0; i < frontend.effectCatalog.length; ++i) {
            var e = frontend.effectCatalog[i]
            if (selectedCategory !== "All Effects" && e.category !== selectedCategory) continue
            var hay = (e.name + " " + e.description + " " + e.category).toLowerCase()
            if (q.length && hay.indexOf(q) < 0) continue
            result.push(e)
        }
        return result
    }
    function refreshModels() {
        categories = buildCategories()
        if (categories.indexOf(selectedCategory) < 0) selectedCategory = "All Effects"
        filteredEffects = filterEffects()
    }
    function openBrowser() {
        fadeOut.stop()
        refreshModels()
        visible = true
        requestActivate()
        opacity = 0
        contentScale = 0.975
        contentLift = 18
        chromeGlow = 0
        if (frontend.animationsEnabled) {
            fadeIn.restart(); scaleIn.restart(); liftIn.restart(); glowIn.restart()
        } else {
            opacity = 1; contentScale = 1; contentLift = 0; chromeGlow = 1
        }
    }
    function closeBrowser() {
        fadeIn.stop()
        if (frontend.animationsEnabled) fadeOut.restart()
        else visible = false
    }

    Connections {
        target: frontend
        function onProjectChanged() { browser.refreshModels() }
    }

    NumberAnimation { id: fadeIn; target: browser; property: "opacity"; from: 0; to: 1; duration: 220; easing.type: Easing.OutCubic }
    NumberAnimation { id: scaleIn; target: browser; property: "contentScale"; from: 0.975; to: 1; duration: 360; easing.type: Easing.OutQuint }
    NumberAnimation { id: liftIn; target: browser; property: "contentLift"; from: 18; to: 0; duration: 390; easing.type: Easing.OutQuint }
    NumberAnimation { id: glowIn; target: browser; property: "chromeGlow"; from: 0; to: 1; duration: 520; easing.type: Easing.OutCubic }
    SequentialAnimation {
        id: fadeOut
        NumberAnimation { target: browser; property: "opacity"; to: 0; duration: 130; easing.type: Easing.InCubic }
        ScriptAction { script: browser.visible = false }
    }

    Rectangle {
        focus: true
        Keys.onEscapePressed: browser.closeBrowser()
        anchors.fill: parent
        color: frontend.themeName === "Liquid Glass"
               ? Qt.rgba(frontend.windowColor.r, frontend.windowColor.g, frontend.windowColor.b, 0.94)
               : Qt.rgba(frontend.windowColor.r, frontend.windowColor.g, frontend.windowColor.b, 0.92)
        radius: 22
        border.width: 1
        border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.17)
        scale: browser.contentScale
        transform: Translate { y: browser.contentLift }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: 21
            color: frontend.themeName === "Liquid Glass"
                   ? Qt.rgba(frontend.panelColor.r, frontend.panelColor.g, frontend.panelColor.b, 0.89)
                   : Qt.rgba(frontend.panelColor.r, frontend.panelColor.g, frontend.panelColor.b, 0.94)
        }

        Column {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 14

            Item {
                width: parent.width; height: 54
                Text { text: "✦"; color: frontend.accentColor; font.pixelSize: 23; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter }
                Column {
                    anchors.left: parent.left; anchors.leftMargin: 38
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text { text: "Add an Effect"; color: frontend.textColor; font.pixelSize: 22; font.bold: true }
                    Text { text: "Choose a visual module built for this shader target."; color: frontend.mutedColor; font.pixelSize: 12 }
                }
                GlassButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: 38; height: 38; compact: true; text: "×"; onClicked: browser.closeBrowser() }
            }

            Rectangle {
                width: parent.width; height: 44; radius: 12
                color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b,
                               frontend.themeName === "Liquid Glass" ? 0.90 : 0.72)
                border.width: 1
                border.color: searchInput.activeFocus ? frontend.accentColor : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.12)
                Text { text: "⌕"; anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; color: frontend.mutedColor; font.pixelSize: 18 }
                TextInput {
                    id: searchInput
                    anchors.left: parent.left; anchors.leftMargin: 42
                    anchors.right: parent.right; anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    color: frontend.textColor
                    font.pixelSize: 13
                    selectByMouse: true
                    text: browser.searchText
                    onTextChanged: { browser.searchText = text; browser.filteredEffects = browser.filterEffects() }
                    Text { visible: !searchInput.text.length && !searchInput.activeFocus; text: "Search glow, grain, depth, color…"; color: frontend.mutedColor; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                }
            }

            Row {
                width: parent.width
                height: parent.height - 126
                spacing: 14

                Rectangle {
                    width: 190; height: parent.height; radius: 15
                    color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b,
                                   frontend.themeName === "Liquid Glass" ? 0.84 : 0.58)
                    border.width: 1
                    border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.08)
                    ListView {
                        anchors.fill: parent; anchors.margins: 8
                        clip: true
                        spacing: 4
                        model: browser.categories
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width; height: 40; radius: 10
                            color: browser.selectedCategory === modelData
                                ? Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.42)
                                : catMouse.containsMouse ? Qt.rgba(frontend.buttonColor.r, frontend.buttonColor.g, frontend.buttonColor.b, 0.55) : "transparent"
                            border.width: browser.selectedCategory === modelData ? 1 : 0
                            border.color: frontend.accentColor
                            Behavior on color { ColorAnimation { duration: frontend.animationsEnabled ? 170 : 0; easing.type: Easing.OutCubic } }
                            Behavior on border.color { ColorAnimation { duration: frontend.animationsEnabled ? 170 : 0 } }
                            Rectangle {
                                width: 3; height: parent.height - 12; radius: 2
                                anchors.left: parent.left; anchors.leftMargin: 3; anchors.verticalCenter: parent.verticalCenter
                                color: frontend.accentColor
                                opacity: browser.selectedCategory === modelData ? 1 : catMouse.containsMouse ? 0.42 : 0
                                Behavior on opacity { NumberAnimation { duration: frontend.animationsEnabled ? 160 : 0 } }
                            }
                            Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: modelData; color: frontend.textColor; font.pixelSize: 12; font.weight: browser.selectedCategory === modelData ? Font.DemiBold : Font.Normal }
                            MouseArea { id: catMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { browser.selectedCategory = modelData; browser.filteredEffects = browser.filterEffects() } }
                        }
                    }
                }

                ListView {
                    id: effectList
                    width: parent.width - 225
                    height: parent.height
                    clip: true
                    spacing: 10
                    boundsBehavior: Flickable.StopAtBounds
                    model: browser.filteredEffects
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        height: 112
                        opacity: 1
                        radius: 15
                        color: cardMouse.containsMouse
                            ? Qt.rgba(frontend.buttonColor.r, frontend.buttonColor.g, frontend.buttonColor.b,
                                      frontend.themeName === "Liquid Glass" ? 0.92 : 0.76)
                            : Qt.rgba(frontend.panelColor.r, frontend.panelColor.g, frontend.panelColor.b,
                                      frontend.themeName === "Liquid Glass" ? 0.86 : 0.64)
                        border.width: cardMouse.containsMouse ? 1.25 : 1
                        border.color: cardMouse.containsMouse ? Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.62) : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.095)
                        Behavior on color { ColorAnimation { duration: frontend.animationsEnabled ? 155 : 0; easing.type: Easing.OutCubic } }
                        Behavior on border.color { ColorAnimation { duration: frontend.animationsEnabled ? 155 : 0; easing.type: Easing.OutCubic } }
                        Rectangle {
                            width: 3; height: parent.height - 22; radius: 2
                            anchors.left: parent.left; anchors.leftMargin: 4; anchors.verticalCenter: parent.verticalCenter
                            color: frontend.accentColor
                            opacity: cardMouse.containsMouse ? 0.86 : 0
                            Behavior on opacity { NumberAnimation { duration: frontend.animationsEnabled ? 170 : 0; easing.type: Easing.OutCubic } }
                        }
                        Column {
                            anchors.left: parent.left; anchors.leftMargin: 16
                            anchors.right: addButton.left; anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6
                            Row {
                                spacing: 9
                                Text { text: modelData.name; color: frontend.textColor; font.pixelSize: 15; font.bold: true }
                                Rectangle {
                                    height: 20; width: tagText.implicitWidth + 14; radius: 7
                                    color: Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.18)
                                    Text { id: tagText; anchors.centerIn: parent; text: String(modelData.category).toUpperCase(); color: frontend.accentColor; font.pixelSize: 9; font.bold: true }
                                }
                            }
                            Text { width: parent.width; text: modelData.description; color: frontend.mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                            Text { text: modelData.usesDepth ? "✓ BO3   •   Float-Z" : "✓ BO3"; color: modelData.usesDepth ? "#7ddba1" : "#79c99a"; font.pixelSize: 10 }
                        }
                        GlassButton {
                            id: addButton
                            anchors.right: parent.right; anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            width: 78; height: 34; compact: true; primary: true; text: "+ Add"
                            onClicked: {
                                frontend.requestAddEffect(modelData.id)
                                browser.closeBrowser()
                            }
                        }
                        MouseArea { id: cardMouse; anchors.fill: parent; anchors.rightMargin: 100; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                    }
                }

                Rectangle {
                    id: scrollTrack
                    width: 8
                    height: parent.height
                    radius: 4
                    color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.06)
                    opacity: effectList.contentHeight > effectList.height + 1 ? 1 : 0
                    enabled: effectList.contentHeight > effectList.height + 1
                    Behavior on opacity { NumberAnimation { duration: frontend.animationsEnabled ? 120 : 0 } }

                    function setFromThumbY(thumbY) {
                        var travel = Math.max(1, height - scrollThumb.height)
                        var contentTravel = Math.max(0, effectList.contentHeight - effectList.height)
                        var clampedY = Math.max(0, Math.min(travel, thumbY))
                        effectList.contentY = (clampedY / travel) * contentTravel
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: scrollTrack.setFromThumbY(mouseY - scrollThumb.height / 2)
                    }

                    Rectangle {
                        id: scrollThumb
                        z: 2
                        width: parent.width
                        radius: 4
                        height: Math.max(36, scrollTrack.height * Math.min(1, effectList.height / Math.max(1, effectList.contentHeight)))
                        y: {
                            var travel = Math.max(0, scrollTrack.height - height)
                            var contentTravel = Math.max(1, effectList.contentHeight - effectList.height)
                            return travel * Math.max(0, Math.min(1, effectList.contentY / contentTravel))
                        }
                        color: thumbMouse.containsMouse || thumbMouse.pressed
                               ? frontend.accentColor
                               : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.34)
                        Behavior on color { ColorAnimation { duration: frontend.animationsEnabled ? 120 : 0 } }
                        MouseArea {
                            id: thumbMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            preventStealing: true
                            cursorShape: Qt.PointingHandCursor
                            property real dragOffset: 0
                            onPressed: {
                                // Store the pointer offset in TRACK coordinates.
                                // Using mouseY relative to the moving thumb causes
                                // the thumb to chase the cursor and feel slower.
                                var p = mapToItem(scrollTrack, mouseX, mouseY)
                                dragOffset = p.y - scrollThumb.y
                            }
                            onPositionChanged: {
                                if (!pressed) return
                                var p = mapToItem(scrollTrack, mouseX, mouseY)
                                scrollTrack.setFromThumbY(p.y - dragOffset)
                            }
                        }
                    }
                }
            }
        }
    }
}
