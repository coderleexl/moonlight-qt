import QtQuick 2.9
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

NavigableDialog {
    id: dialog
    objectName: "aboutDialog"
    property string version
    property string licenseText
    property bool showingLicense: false
    title: showingLicense ? qsTr("Open-source license") : qsTr("About Desk")
    width: Math.min(520, window.width - 32)
    height: Math.min(440, window.height - 48)
    onOpened: showingLicense = false
    header: Label {
        text: dialog.title
        color: window.textColor
        font.pixelSize: 20
        font.weight: Font.Medium
        leftPadding: dialog.leftPadding
        rightPadding: dialog.rightPadding
        topPadding: 20
        bottomPadding: 12
        elide: Text.ElideRight
    }

    contentItem: ColumnLayout {
        spacing: 16
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: dialog.showingLicense ? 1 : 0

            ScrollView {
                id: aboutScroll
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: aboutScroll.availableWidth
                    spacing: 16
                    RowLayout {
                        spacing: 14
                        Image {
                            source: "../res/desk.svg"
                            sourceSize.width: 64
                            sourceSize.height: 64
                            Layout.preferredWidth: 64
                            Layout.preferredHeight: 64
                        }
                        ColumnLayout {
                            spacing: 4
                            Label { text: "Desk"; font.pixelSize: 25; font.weight: Font.DemiBold; color: window.textColor }
                            Label { text: qsTr("Version %1").arg(dialog.version); color: window.secondaryColor; font.pixelSize: 12 }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Maintained by %1").arg("coderleex") + "\n" + qsTr("Desk modifications © 2026 coderleex")
                        color: window.textColor
                        wrapMode: Text.Wrap
                        lineHeight: 1.35
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Based on %1 and %2.")
                            .arg('<a href="https://github.com/moonlight-stream/moonlight-qt">Moonlight Qt</a>')
                            .arg('<a href="https://github.com/LizardByte/Sunshine">Sunshine</a>')
                        textFormat: Text.StyledText
                        color: window.textColor
                        linkColor: window.accentColor
                        wrapMode: Text.Wrap
                        onLinkActivated: function(link) { Qt.openUrlExternally(link); }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Upstream copyrights remain with their respective authors.") + "\n\n"
                            + qsTr("Licensed under GNU GPL v3. You may redistribute and modify this software under that license. Provided without warranty.")
                        color: window.secondaryColor
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        lineHeight: 1.35
                    }
                }
            }
            ScrollView {
                id: licenseScroll
                objectName: "licenseScroll"
                clip: true
                contentWidth: availableWidth
                TextArea {
                    objectName: "licenseText"
                    text: dialog.licenseText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    color: window.textColor
                    font.pixelSize: 12
                    padding: 0
                    background: null
                    Accessible.name: "GNU General Public License v3.0"
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ActionButton {
                text: qsTr("Source code")
                visible: !dialog.showingLicense
                onClicked: Qt.openUrlExternally("https://github.com/coderleexl/moonlight-qt")
            }
            ActionButton {
                objectName: "licenseToggle"
                text: dialog.showingLicense ? qsTr("Back") : qsTr("License")
                onClicked: dialog.showingLicense = !dialog.showingLicense
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                objectName: "aboutClose"
                text: qsTr("Close")
                primary: true
                onClicked: dialog.close()
            }
        }
    }
}
