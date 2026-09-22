import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Item {
    id: indicator
    property url iconSource
    property string text
    property string description
    implicitWidth: content.implicitWidth
    implicitHeight: 24
    Accessible.role: Accessible.StaticText
    Accessible.name: description + ": " + text
    RowLayout {
        id: content
        anchors.fill: parent
        spacing: 8
        ToolButton {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 24
            padding: 0
            enabled: false
            focusPolicy: Qt.NoFocus
            icon.source: indicator.iconSource
            icon.width: 20
            icon.height: 20
            icon.color: window.secondaryColor
            background: Item {}
            Accessible.ignored: true
        }
        Label {
            text: indicator.text
            color: window.textColor
            font.pixelSize: 14
            Accessible.ignored: true
        }
    }
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
    }
    ToolTip.visible: hoverArea.containsMouse && description.length > 0
    ToolTip.delay: 700
    ToolTip.text: description
}
