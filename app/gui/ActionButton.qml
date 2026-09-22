import QtQuick 2.9
import QtQuick.Controls 2.2

Button {
    id: control
    property bool primary: false
    implicitWidth: Math.max(112, implicitContentWidth + 32)
    implicitHeight: 40
    padding: 0
    leftPadding: 16
    rightPadding: 16
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    font.pixelSize: 14
    activeFocusOnTab: true
    opacity: enabled ? 1 : 0.5
    contentItem: Label {
        text: control.text
        font: control.font
        color: control.primary ? (window.darkTheme ? "#181818" : "#FFFFFF") : window.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 6
        color: control.primary ? (control.down ? Qt.darker(window.accentColor, 1.12) : window.accentColor) : control.down ? window.selectionColor : control.hovered ? window.hoverColor : window.darkTheme ? "#313131" : "#EDF2F8"
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? window.accentColor : control.primary ? "transparent" : window.borderColor
    }
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
}
