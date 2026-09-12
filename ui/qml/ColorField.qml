import QtQuick 6.8
import QtQuick.Window 6.8

Item {
    id: root
    property string label: "Color"
    property string settingKey: ""
    property string parameterKey: ""
    property bool parameterMode: false
    property bool showLabel: true
    property color value: "#ffffff"
    property real hue: 0.0
    property bool pickerOpen: false
    width: parent ? parent.width : 320
    height: 42

    function clamp01(v) { return Math.max(0, Math.min(1, v)) }
    function clamp255(v) { return Math.max(0, Math.min(255, Math.round(v))) }

    function hsvFromRgb(c) {
        var r = c.r, g = c.g, b = c.b
        var maxv = Math.max(r, g, b)
        var minv = Math.min(r, g, b)
        var d = maxv - minv
        var h = root.hue
        if (d > 0.000001) {
            if (maxv === r) h = ((g - b) / d) % 6.0
            else if (maxv === g) h = ((b - r) / d) + 2.0
            else h = ((r - g) / d) + 4.0
            h /= 6.0
            if (h < 0) h += 1.0
        }
        var s = maxv <= 0.000001 ? 0.0 : d / maxv
        return { h: h, s: s, v: maxv }
    }

    function rgbFromHsv(h, s, v) {
        h = ((h % 1.0) + 1.0) % 1.0
        s = clamp01(s); v = clamp01(v)
        var i = Math.floor(h * 6.0)
        var f = h * 6.0 - i
        var p = v * (1.0 - s)
        var q = v * (1.0 - f * s)
        var t = v * (1.0 - (1.0 - f) * s)
        switch (i % 6) {
        case 0: return Qt.rgba(v, t, p, 1.0)
        case 1: return Qt.rgba(q, v, p, 1.0)
        case 2: return Qt.rgba(p, v, t, 1.0)
        case 3: return Qt.rgba(p, q, v, 1.0)
        case 4: return Qt.rgba(t, p, v, 1.0)
        default: return Qt.rgba(v, p, q, 1.0)
        }
    }

    function commitColor(c) {
        if (parameterMode)
            frontend.requestParameterColor(parameterKey, c)
        else
            frontend.requestBaseColor(settingKey, c)
    }

    function hex2(v) {
        var s = Math.round(clamp01(v) * 255).toString(16)
        return s.length < 2 ? "0" + s : s
    }

    function normalizedHex() {
        return ("#" + hex2(value.r) + hex2(value.g) + hex2(value.b)).toUpperCase()
    }

    function colorFromHex(textValue) {
        var s = String(textValue).trim()
        if (s.length && s.charAt(0) !== "#") s = "#" + s
        if (!/^#[0-9a-fA-F]{6}$/.test(s)) return null
        var r = parseInt(s.substr(1, 2), 16) / 255.0
        var g = parseInt(s.substr(3, 2), 16) / 255.0
        var b = parseInt(s.substr(5, 2), 16) / 255.0
        return Qt.rgba(r, g, b, 1.0)
    }

    function syncFromValue() {
        var hsv = hsvFromRgb(value)
        if (hsv.s > 0.000001) hue = hsv.h
        if (!fieldHexInput.activeFocus) fieldHexInput.text = normalizedHex()
        if (!hexInput.activeFocus) hexInput.text = normalizedHex()
        if (!redInput.activeFocus) redInput.text = String(clamp255(value.r * 255))
        if (!greenInput.activeFocus) greenInput.text = String(clamp255(value.g * 255))
        if (!blueInput.activeFocus) blueInput.text = String(clamp255(value.b * 255))
    }

    function applySv(px, py) {
        var saturation = clamp01(px / Math.max(1, svArea.width))
        var val = clamp01(1.0 - py / Math.max(1, svArea.height))
        commitColor(rgbFromHsv(hue, saturation, val))
    }

    function commitHex() {
        var c = colorFromHex(hexInput.text)
        if (c !== null) commitColor(c)
        else hexInput.text = normalizedHex()
    }

    function commitRgb() {
        var r = parseInt(redInput.text)
        var g = parseInt(greenInput.text)
        var b = parseInt(blueInput.text)
        if (isNaN(r) || isNaN(g) || isNaN(b)) {
            redInput.text = String(clamp255(value.r * 255))
            greenInput.text = String(clamp255(value.g * 255))
            blueInput.text = String(clamp255(value.b * 255))
            return
        }
        commitColor(Qt.rgba(clamp255(r) / 255.0, clamp255(g) / 255.0, clamp255(b) / 255.0, 1.0))
    }

    function positionPicker() {
        var p = field.mapToGlobal(0, field.height + 7)
        var host = root.Window.window
        pickerOverlay.x = Math.max(host.x, Math.min(host.x + host.width - pickerOverlay.width, p.x))
        pickerOverlay.y = Math.max(host.y, Math.min(host.y + host.height - pickerOverlay.height, p.y))
    }

    function togglePicker() {
        pickerOpen = !pickerOpen
        if (pickerOpen) {
            syncFromValue()
            Qt.callLater(positionPicker)
        }
    }

    onEnabledChanged: if (!enabled) pickerOpen = false
    onVisibleChanged: if (!visible) pickerOpen = false
    onValueChanged: Qt.callLater(syncFromValue)
    Component.onCompleted: syncFromValue()

    Text {
        visible: root.showLabel
        anchors.left: parent.left
        anchors.verticalCenter: field.verticalCenter
        text: root.label
        color: frontend.textColor
        font.pixelSize: 11
        font.weight: Font.Medium
    }

    Rectangle {
        id: field
        x: root.showLabel ? root.width - 150 : 0
        y: 4
        width: root.showLabel ? 150 : root.width
        height: 34
        radius: 9
        color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.66)
        border.width: 1
        border.color: root.pickerOpen || fieldHexInput.activeFocus
                      ? frontend.accentColor
                      : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.11)

        Rectangle {
            id: swatch
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 23
            height: 23
            radius: 6
            color: root.value
            border.width: swatchMouse.containsMouse ? 2 : 1
            border.color: swatchMouse.containsMouse ? frontend.accentColor : Qt.rgba(1, 1, 1, 0.28)
            MouseArea {
                id: swatchMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.togglePicker()
            }
        }

        TextInput {
            id: fieldHexInput
            anchors.left: parent.left
            anchors.leftMargin: 38
            anchors.right: parent.right
            anchors.rightMargin: 23
            anchors.verticalCenter: parent.verticalCenter
            text: root.normalizedHex()
            color: frontend.textColor
            font.pixelSize: 11
            selectByMouse: true
            onEditingFinished: {
                var c = root.colorFromHex(text)
                if (c !== null) root.commitColor(c)
                else text = root.normalizedHex()
            }
            Keys.onReturnPressed: {
                var c = root.colorFromHex(text)
                if (c !== null) root.commitColor(c)
                else text = root.normalizedHex()
                focus = false
            }
            Keys.onEscapePressed: {
                text = root.normalizedHex()
                focus = false
                root.pickerOpen = false
            }
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.pickerOpen ? "⌃" : "⌄"
            color: frontend.mutedColor
            font.pixelSize: 11
        }
        MouseArea {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 28
            cursorShape: Qt.PointingHandCursor
            onClicked: root.togglePicker()
        }
    }

    Window {
        id: pickerOverlay
        transientParent: root.Window.window
        flags: Qt.Popup | Qt.FramelessWindowHint
        color: "transparent"
        width: 300
        height: 276
        visible: root.pickerOpen
        onVisibleChanged: {
            if (!visible) root.pickerOpen = false
            else { root.positionPicker(); requestActivate() }
        }
        onClosing: root.pickerOpen = false

        Rectangle {
            id: pickerPanel
            width: 300
            height: 276
            focus: true
            Keys.onEscapePressed: root.pickerOpen = false
            radius: 13
            color: Qt.rgba(frontend.panelColor.r, frontend.panelColor.g, frontend.panelColor.b,
                           frontend.themeName === "Liquid Glass" ? 0.90 : 0.985)
            border.width: 1
            border.color: frontend.themeName === "Liquid Glass"
                          ? Qt.rgba(0.94, 0.99, 1.0, 0.34)
                          : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.18)

            // Eat clicks inside the picker so the click-away layer behind it does
            // not close the panel while a control is being manipulated.
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.leftMargin: 14; anchors.rightMargin: 14
                height: 1
                color: Qt.rgba(1, 1, 1, frontend.themeName === "Liquid Glass" ? 0.34 : 0.10)
            }

            Text {
                x: 12; y: 9
                text: root.label.length ? root.label : "Color"
                color: frontend.textColor
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 12; y: 9
                text: root.normalizedHex()
                color: frontend.mutedColor
                font.pixelSize: 10
            }

            Rectangle {
                id: svArea
                x: 12; y: 32
                width: 176
                height: 148
                radius: 8
                color: root.rgbFromHsv(root.hue, 1.0, 1.0)
                clip: true
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.16)

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "white" }
                        GradientStop { position: 1; color: "transparent" }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 1; color: "black" }
                    }
                }
                Rectangle {
                    readonly property var hsv: root.hsvFromRgb(root.value)
                    width: 12; height: 12; radius: 6
                    x: Math.max(0, Math.min(svArea.width - width, hsv.s * svArea.width - width / 2))
                    y: Math.max(0, Math.min(svArea.height - height, (1.0 - hsv.v) * svArea.height - height / 2))
                    color: "transparent"
                    border.width: 2
                    border.color: "white"
                    Rectangle { anchors.fill: parent; anchors.margins: -1; radius: parent.radius; color: "transparent"; border.width: 1; border.color: "#66000000" }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.CrossCursor
                    onPressed: root.applySv(mouseX, mouseY)
                    onPositionChanged: if (pressed) root.applySv(mouseX, mouseY)
                }
            }

            Rectangle {
                id: hueBar
                x: 196; y: 32
                width: 18; height: 148; radius: 9
                gradient: Gradient {
                    GradientStop { position: 0.000; color: "#ff0000" }
                    GradientStop { position: 0.167; color: "#ffff00" }
                    GradientStop { position: 0.333; color: "#00ff00" }
                    GradientStop { position: 0.500; color: "#00ffff" }
                    GradientStop { position: 0.667; color: "#0000ff" }
                    GradientStop { position: 0.833; color: "#ff00ff" }
                    GradientStop { position: 1.000; color: "#ff0000" }
                }
                Rectangle {
                    width: 22; height: 7; radius: 3.5
                    x: -2
                    y: Math.max(0, Math.min(hueBar.height - height, root.hue * hueBar.height - height / 2))
                    color: "transparent"
                    border.width: 2
                    border.color: "white"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    function apply(py) {
                        var hsv = root.hsvFromRgb(root.value)
                        root.hue = root.clamp01(py / Math.max(1, height))
                        root.commitColor(root.rgbFromHsv(root.hue, hsv.s, hsv.v))
                    }
                    onPressed: apply(mouseY)
                    onPositionChanged: if (pressed) apply(mouseY)
                }
            }

            Column {
                x: 224; y: 32
                width: 64
                spacing: 7
                Rectangle {
                    width: 64; height: 38; radius: 8
                    color: root.value
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.30)
                }
                Text { text: "R"; color: frontend.mutedColor; font.pixelSize: 8 }
                Rectangle {
                    width: 64; height: 25; radius: 6
                    color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.72)
                    border.width: 1; border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.12)
                    TextInput { id: redInput; objectName: "redInput"; anchors.fill: parent; anchors.margins: 5; color: frontend.textColor; font.pixelSize: 10; horizontalAlignment: TextInput.AlignHCenter; validator: IntValidator { bottom: 0; top: 255 }
                        onEditingFinished: root.commitRgb(); Keys.onReturnPressed: { root.commitRgb(); focus = false } }
                }
                Text { text: "G"; color: frontend.mutedColor; font.pixelSize: 8 }
                Rectangle {
                    width: 64; height: 25; radius: 6
                    color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.72)
                    border.width: 1; border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.12)
                    TextInput { id: greenInput; objectName: "greenInput"; anchors.fill: parent; anchors.margins: 5; color: frontend.textColor; font.pixelSize: 10; horizontalAlignment: TextInput.AlignHCenter; validator: IntValidator { bottom: 0; top: 255 }
                        onEditingFinished: root.commitRgb(); Keys.onReturnPressed: { root.commitRgb(); focus = false } }
                }
                Text { text: "B"; color: frontend.mutedColor; font.pixelSize: 8 }
                Rectangle {
                    width: 64; height: 25; radius: 6
                    color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.72)
                    border.width: 1; border.color: Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.12)
                    TextInput { id: blueInput; objectName: "blueInput"; anchors.fill: parent; anchors.margins: 5; color: frontend.textColor; font.pixelSize: 10; horizontalAlignment: TextInput.AlignHCenter; validator: IntValidator { bottom: 0; top: 255 }
                        onEditingFinished: root.commitRgb(); Keys.onReturnPressed: { root.commitRgb(); focus = false } }
                }
            }

            Text { x: 12; y: 190; text: "HEX"; color: frontend.mutedColor; font.pixelSize: 8 }
            Rectangle {
                x: 42; y: 185
                width: 146; height: 29; radius: 7
                color: Qt.rgba(frontend.baseColor.r, frontend.baseColor.g, frontend.baseColor.b, 0.72)
                border.width: 1
                border.color: hexInput.activeFocus ? frontend.accentColor : Qt.rgba(frontend.textColor.r, frontend.textColor.g, frontend.textColor.b, 0.12)
                TextInput {
                    id: hexInput
                    objectName: "hexInput"
                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                    verticalAlignment: TextInput.AlignVCenter
                    color: frontend.textColor
                    font.pixelSize: 10
                    selectByMouse: true
                    onEditingFinished: root.commitHex()
                    Keys.onReturnPressed: { root.commitHex(); focus = false }
                    Keys.onEscapePressed: { text = root.normalizedHex(); focus = false; root.pickerOpen = false }
                }
            }
            GlassButton {
                x: 224; y: 235
                width: 64; height: 29; compact: true
                text: "Done"
                onClicked: root.pickerOpen = false
            }
        }
    }

}
