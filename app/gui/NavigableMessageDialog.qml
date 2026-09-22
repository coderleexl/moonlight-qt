import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2

NavigableDialog {
    id: dialog

    property alias text: dialogLabel.dialogText
    property alias showSpinner: dialogSpinner.visible
    property alias imageSrc: dialogImage.source
    property string helpText
    property string helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Troubleshooting"
    property string helpTextSeparator: " "

    onOpened: {
        if (dialogButtonBox.count > 0) {
            dialogButtonBox.itemAt(dialogButtonBox.count - 1).forceActiveFocus(Qt.TabFocusReason);
        }
    }

    contentItem: RowLayout {
        spacing: 16
        BusyIndicator {
            id: dialogSpinner
            visible: false
            running: visible
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
        }
        // Keep the existing imageSrc API while tinting icons for both appearances.
        Image {
            id: dialogImage
            visible: false
            source: (standardButtons & Dialog.Yes) ? "qrc:/res/baseline-help_outline-24px.svg" : "qrc:/res/baseline-error_outline-24px.svg"
        }
        ToolButton {
            visible: !dialog.showSpinner
            enabled: false
            focusPolicy: Qt.NoFocus
            icon.source: dialogImage.source
            icon.width: 32
            icon.height: 32
            icon.color: window.accentColor
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
        }
        ScrollView {
            id: messageScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(dialogLabel.implicitHeight, Math.max(80, window.height - 220))
            clip: true
            contentWidth: availableWidth
            Label {
                id: dialogLabel
                property string dialogText
                width: messageScroll.availableWidth
                text: dialogText + ((helpText && (standardButtons & Dialog.Help)) ? (helpTextSeparator + helpText) : "")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
        }
    }

    footer: DialogButtonBox {
        id: dialogButtonBox
        standardButtons: dialog.standardButtons
        padding: 12
        delegate: Button {
            flat: true
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            Keys.onRightPressed: nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocusReason)
            Keys.onLeftPressed: nextItemInFocusChain(false).forceActiveFocus(Qt.TabFocusReason)
        }
        onHelpRequested: {
            Qt.openUrlExternally(helpUrl);
            dialog.close();
        }
    }
}
