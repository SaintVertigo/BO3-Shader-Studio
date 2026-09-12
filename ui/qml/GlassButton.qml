import QtQuick 6.8

Rectangle {
    id: root
    property string text: ""
    property string glyph: ""
    property bool checked: false
    property bool primary: false
    property bool compact: false
    signal clicked()

    implicitWidth: Math.max(compact ? 72 : 96, label.implicitWidth + (glyph.length ? 46 : 28))
    implicitHeight: compact ? 34 : 40
    radius: compact ? 10 : 12
    opacity: enabled ? 1.0 : 0.42
    color: primary || checked
           ? Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, mouse.pressed ? 0.72 : mouse.containsMouse ? 0.62 : 0.52)
           : Qt.rgba(frontend.buttonColor.r, frontend.buttonColor.g, frontend.buttonColor.b, mouse.containsMouse ? 0.86 : 0.68)
    border.width: primary || checked ? 1.35 : 1
    border.color: primary || checked
                  ? Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.95)
                  : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, mouse.containsMouse ? 0.22 : 0.10)
    scale: mouse.pressed ? 0.965 : mouse.containsMouse ? 1.015 : 1.0

    Behavior on color { ColorAnimation { duration: frontend.animationsEnabled ? 130 : 0 } }
    Behavior on scale { NumberAnimation { duration: frontend.animationsEnabled ? 115 : 0; easing.type: Easing.OutCubic } }
    Behavior on border.color { ColorAnimation { duration: frontend.animationsEnabled ? 130 : 0 } }

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
