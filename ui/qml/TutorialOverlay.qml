import QtQuick 6.8
import QtQuick.Window 6.8

Window {
    id: tour
    visible: false
    color: "transparent"
    flags: Qt.Dialog | Qt.FramelessWindowHint
    modality: Qt.ApplicationModal
    width: transientParent ? transientParent.width : 1600
    height: transientParent ? transientParent.height : 900
    x: transientParent ? transientParent.x : 100
    y: transientParent ? transientParent.y : 80
    property int step: 0
    property real leftWidth: 360
    property int totalSteps: 6

    function titleFor(i) {
        return ["Welcome to Shader Studio", "Choose what you're building", "Build your effect stack", "Tune the selected effect", "Keep the preview in view", "Export when you're ready"][i]
    }
    function bodyFor(i) {
        return [
            "The new front end keeps the creative workflow in motion while the proven BO3 backend stays underneath.",
            "Screen, Material, and Sky each expose only the controls and effects that actually apply to that target.",
            "Open the effect library, add visual modules, toggle them, and reorder them without touching HLSL.",
            "The focused controls live in the rail. Sliders update the live preview without rebuilding the whole interface.",
            "The native Direct3D preview stays dominant. Material preview uses APE Match as the visible reference path.",
            "Export still uses the existing validated BO3 packaging pipeline. The redesign doesn't replace that backend."
        ][i]
    }
    function hx() {
        if (step === 0) return width * 0.5 - 120
        if (step === 1) return 18
        if (step === 2) return 18
        if (step === 3) return 18
        if (step === 4) return leftWidth + 28
        return width - 178
    }
    function hy() {
        if (step === 0) return 40
        if (step === 1) return 142
        if (step === 2) return 450
        if (step === 3) return Math.min(height - 300, 650)
        if (step === 4) return 118
        return 40
    }
    function hw() {
        if (step === 0) return 240
        if (step >= 1 && step <= 3) return Math.max(300, leftWidth - 24)
        if (step === 4) return Math.max(430, width - leftWidth - 54)
        return 158
    }
    function hh() {
        if (step === 0) return 48
        if (step === 1) return 145
        if (step === 2) return 170
        if (step === 3) return 190
        if (step === 4) return Math.max(280, height - 170)
        return 48
    }

    function openTour() {
        step = 0
        visible = true
        card.opacity = 0
        card.scale = 0.97
        if (frontend.animationsEnabled) enterAnim.restart()
        else { card.opacity = 1; card.scale = 1 }
    }
    function finish() {
        frontend.requestTutorialComplete()
        if (frontend.animationsEnabled) exitAnim.restart()
        else visible = false
    }

    Rectangle {
        anchors.fill: parent
        color: "#b8000000"
        MouseArea { anchors.fill: parent }
    }

    Rectangle {
        id: ring
        x: tour.hx(); y: tour.hy(); width: tour.hw(); height: tour.hh()
        radius: 15
        color: Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.07)
        border.width: 2
        border.color: frontend.accentColor
        Behavior on x { NumberAnimation { duration: frontend.animationsEnabled ? 420 : 0; easing.type: Easing.OutQuint } }
        Behavior on y { NumberAnimation { duration: frontend.animationsEnabled ? 420 : 0; easing.type: Easing.OutQuint } }
        Behavior on width { NumberAnimation { duration: frontend.animationsEnabled ? 420 : 0; easing.type: Easing.OutQuint } }
        Behavior on height { NumberAnimation { duration: frontend.animationsEnabled ? 420 : 0; easing.type: Easing.OutQuint } }
        SequentialAnimation on opacity {
            running: tour.visible && frontend.animationsEnabled
            loops: Animation.Infinite
            NumberAnimation { to: 0.58; duration: 800; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 800; easing.type: Easing.InOutSine }
        }
    }

    GlassSurface {
        id: card
        width: Math.min(560, parent.width - 40)
        height: 150
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        glassOpacity: 0.97
        highlighted: true
        z: 2

        Column {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 7
            Row {
                width: parent.width
                Text { text: "GETTING STARTED  ·  " + (tour.step + 1) + "/" + tour.totalSteps; color: frontend.accentColor; font.pixelSize: 9; font.bold: true; font.letterSpacing: 0.8 }
            }
            Text { text: tour.titleFor(tour.step); color: frontend.textColor; font.pixelSize: 17; font.bold: true }
            Text { width: parent.width; text: tour.bodyFor(tour.step); color: frontend.mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap }
            Item { width: 1; height: 2 }
            Row {
                width: parent.width
                spacing: 7
                GlassButton { width: 74; height: 30; compact: true; text: "Skip"; onClicked: tour.finish() }
                Item { width: Math.max(0, parent.width - 240); height: 1 }
                GlassButton { width: 72; height: 30; compact: true; text: "Back"; enabled: tour.step > 0; onClicked: tour.step = Math.max(0, tour.step - 1) }
                GlassButton { width: 80; height: 30; compact: true; primary: true; text: tour.step + 1 === tour.totalSteps ? "Finish" : "Next"; onClicked: { if (tour.step + 1 === tour.totalSteps) tour.finish(); else tour.step++ } }
            }
        }
    }

    ParallelAnimation {
        id: enterAnim
        NumberAnimation { target: card; property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
        NumberAnimation { target: card; property: "scale"; from: 0.97; to: 1; duration: 300; easing.type: Easing.OutBack; easing.overshoot: 0.55 }
    }
    SequentialAnimation {
        id: exitAnim
        NumberAnimation { target: card; property: "opacity"; to: 0; duration: 120 }
        ScriptAction { script: tour.visible = false }
    }
}
