import QtQuick 6.8

Rectangle {
    id: root
    property string text: ""
    property string glyph: ""
    property bool checked: false
    property bool primary: false
    property bool compact: false
    readonly property bool liquidGlass: frontend.themeName === "Liquid Glass"
    signal clicked()

    implicitWidth: Math.max(compact ? 72 : 96, label.implicitWidth + (glyph.length ? 46 : 28))
    implicitHeight: compact ? 34 : 40
    radius: compact ? 8 : 10
    opacity: enabled ? 1.0 : 0.42
    color: primary || checked
           ? Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b,
                     liquidGlass ? (mouse.pressed ? 0.62 : mouse.containsMouse ? 0.54 : 0.46)
                                 : (mouse.pressed ? 0.66 : mouse.containsMouse ? 0.56 : 0.46))
           : liquidGlass
             ? Qt.rgba(frontend.buttonColor.r, frontend.buttonColor.g, frontend.buttonColor.b,
                       mouse.containsMouse ? 0.68 : 0.56)
             : Qt.rgba(frontend.buttonColor.r, frontend.buttonColor.g, frontend.buttonColor.b, mouse.containsMouse ? 0.82 : 0.64)
    border.width: primary || checked ? 1.35 : 1
    border.color: primary || checked
                  ? (liquidGlass ? Qt.rgba(0.92, 0.99, 1.0, 0.80)
                                 : Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.95))
                  : liquidGlass
                    ? Qt.rgba(0.94, 0.99, 1.0, mouse.containsMouse ? 0.34 : 0.22)
                    : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, mouse.containsMouse ? 0.22 : 0.10)
    // Never grow beyond our layout cell on hover. Many buttons live inside clipped
    // Flickable/ListView viewports, so >1 hover scaling visibly sliced their edges.
    // Keep motion entirely inside the button and reserve scale for press feedback.
    scale: mouse.pressed ? 0.975 : 1.0

    Behavior on color { ColorAnimation { duration: frontend.animationsEnabled ? 150 : 0; easing.type: Easing.OutCubic } }
    Behavior on scale { NumberAnimation { duration: frontend.animationsEnabled ? 105 : 0; easing.type: Easing.OutCubic } }
    Behavior on border.color { ColorAnimation { duration: frontend.animationsEnabled ? 150 : 0; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(0, root.radius - 1)
        color: "transparent"
        border.width: 1
        border.color: root.liquidGlass
                      ? Qt.rgba(1, 1, 1, mouse.containsMouse ? 0.18 : 0.07)
                      : Qt.rgba(1, 1, 1, mouse.containsMouse ? 0.12 : 0.0)
        opacity: root.liquidGlass || mouse.containsMouse ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: frontend.animationsEnabled ? 160 : 0; easing.type: Easing.OutCubic } }
    }

    Rectangle {
        visible: root.liquidGlass
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.radius * 0.6
        anchors.rightMargin: root.radius * 0.6
        height: 1
        color: Qt.rgba(1, 1, 1, 0.30)
    }

    Row {
        anchors.centerIn: parent
        spacing: 8
        Text {
            visible: root.glyph.length > 0
            text: root.glyph
            color: root.primary || root.checked ? "white" : frontend.textColor
            font.pixelSize: root.compact ? 13 : 14
            font.bold: true
        }
        Text {
            id: label
            text: root.text
            color: root.primary || root.checked ? "white" : frontend.textColor
            font.pixelSize: root.compact ? 12 : 13
            font.weight: root.primary || root.checked ? Font.DemiBold : Font.Medium
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
