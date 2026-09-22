import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Controls.Material 2.2

Rectangle {
    id: status
    property alias text: stageLabel.text
    property alias busy: stageSpinner.visible
    color: window.contentColor
    Material.theme: window.darkTheme ? Material.Dark : Material.Light
    Material.accent: window.accentColor
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 48)
        spacing: 20
        BusyIndicator {
            id: stageSpinner
            objectName: "connectionSpinner"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            running: visible && status.visible
        }
        Label {
            id: stageLabel
            objectName: "connectionStageText"
            Layout.fillWidth: true
            textFormat: Text.PlainText
            color: window.textColor
            font.pixelSize: 18
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
    }
}
