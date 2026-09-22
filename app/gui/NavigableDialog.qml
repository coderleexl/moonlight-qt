import QtQuick 2.0
import QtQuick.Controls 2.5

Dialog {
    id: navigableDialog
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, window.width - 32)
    padding: 24
    property var returnFocusItem: null

    background: Rectangle {
        color: window.surfaceColor
        radius: 12
        border.color: window.borderColor
    }

    onAboutToShow: returnFocusItem = window.activeFocusItem
    onClosed: {
        // Popup finishes its own focus cleanup after closed; restore on the next turn.
        var previous = returnFocusItem;
        returnFocusItem = null;
        Qt.callLater(function () {
            if (previous && previous.visible && previous.enabled) {
                previous.forceActiveFocus(Qt.OtherFocusReason);
            } else {
                window.focusPage();
            }
        });
    }
}
