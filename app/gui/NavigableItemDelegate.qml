import QtQuick 2.0
import QtQuick.Controls 2.2

ItemDelegate {
    // Both GridView and ListView expose currentItem/currentIndex.
    property var grid
    property bool listNavigation: false
    activeFocusOnTab: true
    onActiveFocusChanged: {
        if (activeFocus && grid)
            grid.currentIndex = index;
    }

    highlighted: (grid.activeFocus || activeFocus) && grid.currentItem === this

    Keys.onLeftPressed: {
        if (!listNavigation)
            grid.moveCurrentIndexLeft();
        else
            event.accepted = false;
    }
    Keys.onRightPressed: {
        if (!listNavigation)
            grid.moveCurrentIndexRight();
        else
            event.accepted = false;
    }
    Keys.onDownPressed: {
        if (listNavigation && grid.incrementCurrentIndex)
            grid.incrementCurrentIndex();
        else
            grid.moveCurrentIndexDown();
    }
    Keys.onUpPressed: {
        if (listNavigation && grid.decrementCurrentIndex)
            grid.decrementCurrentIndex();
        else
            grid.moveCurrentIndexUp();

        // If we've reached the top of the grid, move focus to the toolbar
        if (grid.currentItem === this) {
            settingsButton.forceActiveFocus(Qt.TabFocusReason);
        }
    }
    Keys.onReturnPressed: {
        clicked();
    }
    Keys.onEnterPressed: {
        clicked();
    }
}
