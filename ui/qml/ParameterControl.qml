import QtQuick 6.8

Item {
    id: root
    property var parameterData
    property string effectInstanceId: ""
    width: parent ? parent.width : 320
    height: parameterData && parameterData.kind === "float" ? 74 : 66

    Text {
        id: title
        anchors.left: parent.left
        anchors.top: parent.top
        text: parameterData ? parameterData.name : ""
        color: frontend.textColor
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }

    Text {
        anchors.right: parent.right
        anchors.verticalCenter: title.verticalCenter
        text: {
            if (!parameterData) return ""
            if (parameterData.kind === "float") return Number(parameterData.value).toFixed(parameterData.step < 0.1 ? 2 : 1)
            if (parameterData.kind === "choice") return parameterData.choices[parameterData.choiceIndex] || ""
            return String(parameterData.color || "")
        }
        color: frontend.mutedColor
        font.pixelSize: 11
    }

    Item {
        visible: parameterData && parameterData.kind === "float"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: title.bottom
        anchors.topMargin: 15
        height: 28

        Rectangle {
            id: track
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: 5
            radius: 3
            color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.12)
            Rectangle {
                width: {
                    if (!parameterData) return 0
                    var span = Math.max(0.000001, parameterData.maximum - parameterData.minimum)
                    return track.width * Math.max(0, Math.min(1, (parameterData.value - parameterData.minimum) / span))
                }
                height: parent.height
                radius: parent.radius
                color: frontend.accentColor
                Behavior on width { NumberAnimation { duration: frontend.animationsEnabled ? 90 : 0 } }
            }
        }
        Rectangle {
            id: thumb
            width: 16; height: 16; radius: 8
            y: (parent.height - height) / 2
            x: {
                if (!parameterData) return 0
                var span = Math.max(0.000001, parameterData.maximum - parameterData.minimum)
                return Math.max(0, Math.min(parent.width - width, ((parameterData.value - parameterData.minimum) / span) * (parent.width - width)))
            }
            color: frontend.textColor
            border.width: 3
            border.color: frontend.accentColor
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            function apply(px) {
                if (!parameterData) return
                var t = Math.max(0, Math.min(1, px / Math.max(1, width)))
                var raw = parameterData.minimum + t * (parameterData.maximum - parameterData.minimum)
                var step = Math.max(0.000001, parameterData.step)
                var snapped = Math.round(raw / step) * step
                frontend.requestParameterValue(parameterData.key, snapped)
            }
            onPressed: apply(mouseX)
            onPositionChanged: if (pressed) apply(mouseX)
        }
    }

    Rectangle {
        visible: parameterData && parameterData.kind === "color"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: title.bottom
        anchors.topMargin: 10
        height: 34
        radius: 9
        color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.72)
        border.width: 1
        border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.12)
        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 7
            anchors.verticalCenter: parent.verticalCenter
            width: 22; height: 22; radius: 6
            color: parameterData ? parameterData.color : "white"
            border.width: 1
            border.color: Qt.rgba(1,1,1,0.28)
        }
        TextInput {
            id: colorInput
            anchors.left: parent.left
            anchors.leftMargin: 38
            anchors.right: parent.right
            anchors.rightMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            text: parameterData ? String(parameterData.color) : "#ffffff"
            color: frontend.textColor
            selectByMouse: true
            font.pixelSize: 12
            onEditingFinished: frontend.requestParameterColor(parameterData.key, text)
        }
    }

    Rectangle {
        visible: parameterData && parameterData.kind === "choice"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: title.bottom
        anchors.topMargin: 10
        height: 34
        radius: 9
        color: Qt.rgba(frontend.buttonColor.r, frontend.buttonColor.g, frontend.buttonColor.b, 0.70)
        border.width: 1
        border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.12)
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            text: parameterData && parameterData.choices.length ? parameterData.choices[parameterData.choiceIndex] : ""
            color: frontend.textColor
            font.pixelSize: 12
        }
        Text { anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; text: "›"; color: frontend.mutedColor; font.pixelSize: 18 }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (!parameterData || !parameterData.choices.length) return
                var next = (parameterData.choiceIndex + 1) % parameterData.choices.length
                frontend.requestParameterValue(parameterData.key, next)
            }
        }
    }
}
