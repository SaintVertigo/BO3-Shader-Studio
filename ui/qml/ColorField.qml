import QtQuick 6.8

Item {
    id: root
    property string label: "Color"
    property string settingKey: ""
    property color value: "#ffffff"
    width: parent ? parent.width : 320
    height: 42

    Text {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        color: frontend.textColor
        font.pixelSize: 11
        font.weight: Font.Medium
    }

    Rectangle {
        id: field
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 150
        height: 34
        radius: 10
        color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.58)
        border.width: 1
        border.color: input.activeFocus ? frontend.accentColor : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.09)
        Behavior on border.color { ColorAnimation { duration: frontend.animationsEnabled ? 120 : 0 } }

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 23
            height: 23
            radius: 7
            color: root.value
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.25)
        }

        TextInput {
            id: input
            anchors.left: parent.left
            anchors.leftMargin: 38
            anchors.right: parent.right
            anchors.rightMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            text: String(root.value)
            color: frontend.textColor
            font.pixelSize: 11
            selectByMouse: true
            onEditingFinished: frontend.requestBaseColor(root.settingKey, text)
        }
    }
}
