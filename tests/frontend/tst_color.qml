import QtQuick 6.8
import QtTest 1.3
import "../../ui/qml"

TestCase {
    id: testCase
    name: "FrontendColor"
    width: 600
    height: 400
    when: windowShown
    QtObject {
        id: frontend
        property string themeName: "BO3 Dark"
        property bool animationsEnabled: false
        property color textColor: "white"
        property color mutedColor: "gray"
        property color panelColor: "#202020"
        property color baseColor: "#101010"
        property color buttonColor: "#303030"
        property color accentColor: "cyan"
        function requestBaseColor(key, color) { field.value = color }
        function requestParameterColor(key, color) { field.value = color }
    }
    ColorField { id: field; width: 320; settingKey: "baseColor" }

    function test_huePrimaries_data() {
        return [{tag:"red", h:0, hex:"#FF0000"}, {tag:"yellow", h:1/6, hex:"#FFFF00"},
                {tag:"green", h:2/6, hex:"#00FF00"}, {tag:"cyan", h:3/6, hex:"#00FFFF"},
                {tag:"blue", h:4/6, hex:"#0000FF"}, {tag:"magenta", h:5/6, hex:"#FF00FF"}]
    }
    function test_huePrimaries(data) {
        field.commitColor(field.rgbFromHsv(data.h, 1, 1))
        compare(field.normalizedHex(), data.hex)
        fuzzyCompare(field.hsvFromRgb(field.value).h, data.h, 0.0001)
    }
    function test_hexEntry() {
        field.commitColor(field.colorFromHex("12aBcD"))
        compare(field.normalizedHex(), "#12ABCD")
        compare(field.colorFromHex("#12345G"), null)
        compare(field.colorFromHex("#123"), null)
    }
    function test_rgbEntry() {
        findChild(field, "redInput").text = "255"
        findChild(field, "greenInput").text = "0"
        findChild(field, "blueInput").text = "255"
        field.commitRgb()
        compare(field.normalizedHex(), "#FF00FF")
    }
    function test_hexFieldCommit() {
        findChild(field, "hexInput").text = "#00FFFF"
        field.commitHex()
        compare(field.normalizedHex(), "#00FFFF")
        findChild(field, "hexInput").text = "invalid"
        field.commitHex()
        compare(findChild(field, "hexInput").text, "#00FFFF")
    }
    function test_achromaticHue() {
        field.hue = 0.5
        compare(field.hsvFromRgb(Qt.rgba(0, 0, 0, 1)).h, 0.5)
        compare(field.hsvFromRgb(Qt.rgba(1, 1, 1, 1)).h, 0.5)
    }
    function test_popupSurvivesColorUpdate() {
        field.togglePicker()
        verify(field.pickerOpen)
        field.commitColor("#00ff00")
        wait(20)
        verify(field.pickerOpen)
        field.pickerOpen = false
    }
}
