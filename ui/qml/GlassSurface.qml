import QtQuick 6.8

Rectangle {
    id: root
    property color tint: frontend.panelColor
    property real glassOpacity: 0.86
    property real cornerRadius: 18
    property bool highlighted: false
    property color edgeColor: highlighted ? frontend.accentColor : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.12)

    radius: cornerRadius
    color: Qt.rgba(tint.r, tint.g, tint.b, glassOpacity)
    border.width: highlighted ? 1.5 : 1
    border.color: edgeColor

    gradient: Gradient {
        GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, root.highlighted ? 0.105 : 0.065) }
        GradientStop { position: 0.34; color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, root.glassOpacity) }
        GradientStop { position: 1.0; color: Qt.rgba(root.tint.r * 0.82, root.tint.g * 0.82, root.tint.b * 0.82, Math.min(0.97, root.glassOpacity + 0.06)) }
    }
}
