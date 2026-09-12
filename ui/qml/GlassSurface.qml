import QtQuick 6.8

Rectangle {
    id: root
    property color tint: frontend.panelColor
    property real glassOpacity: 0.86
    property real cornerRadius: 18
    property bool highlighted: false
    readonly property bool liquidGlass: frontend.themeName === "Liquid Glass"
    property color edgeColor: highlighted
                              ? frontend.accentColor
                              : liquidGlass
                                ? Qt.rgba(0.92, 0.98, 1.0, 0.34)
                                : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.12)

    radius: cornerRadius
    // Keep Liquid Glass readable. Qt Quick cannot cheaply blur arbitrary native
    // content behind these panels, so use controlled translucency plus specular
    // rims instead of making the interface see-through.
    color: liquidGlass
           ? Qt.rgba(tint.r, tint.g, tint.b, Math.max(0.72, Math.min(0.92, glassOpacity)))
           : Qt.rgba(tint.r, tint.g, tint.b, glassOpacity)
    border.width: highlighted ? 1.5 : 1
    border.color: edgeColor

    gradient: Gradient {
        GradientStop {
            position: 0.0
            color: root.liquidGlass
                   ? Qt.rgba(Math.min(1, root.tint.r * 1.38), Math.min(1, root.tint.g * 1.38), Math.min(1, root.tint.b * 1.38), root.highlighted ? 0.90 : 0.84)
                   : Qt.rgba(1, 1, 1, root.highlighted ? 0.105 : 0.065)
        }
        GradientStop {
            position: 0.16
            color: root.liquidGlass
                   ? Qt.rgba(Math.min(1, root.tint.r * 1.16), Math.min(1, root.tint.g * 1.16), Math.min(1, root.tint.b * 1.16), 0.86)
                   : Qt.rgba(root.tint.r, root.tint.g, root.tint.b, root.glassOpacity)
        }
        GradientStop {
            position: 0.64
            color: root.liquidGlass
                   ? Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.88)
                   : Qt.rgba(root.tint.r, root.tint.g, root.tint.b, root.glassOpacity)
        }
        GradientStop {
            position: 1.0
            color: root.liquidGlass
                   ? Qt.rgba(root.tint.r * 0.78, root.tint.g * 0.82, root.tint.b * 0.90, 0.92)
                   : Qt.rgba(root.tint.r * 0.82, root.tint.g * 0.82, root.tint.b * 0.82, Math.min(0.97, root.glassOpacity + 0.06))
        }
    }

    // Thin internal specular rims make the glass edge catch the background like
    // the Windows/iOS "liquid glass" treatment without taking space from content.
    Rectangle {
        visible: root.liquidGlass
        z: 1000
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.cornerRadius * 0.55
        anchors.rightMargin: root.cornerRadius * 0.55
        height: 1
        color: Qt.rgba(1, 1, 1, 0.24)
        opacity: 0.72
    }
    Rectangle {
        visible: root.liquidGlass
        z: 1000
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.cornerRadius * 0.75
        anchors.rightMargin: root.cornerRadius * 0.75
        height: 1
        color: Qt.rgba(frontend.accentColor.r, frontend.accentColor.g, frontend.accentColor.b, 0.14)
    }
}
