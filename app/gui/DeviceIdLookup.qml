import QtQuick 2.9

// Keep a request alive while mDNS resolves addresses and serverinfo supplies IDs.
QtObject {
    id: lookup
    property var model
    property int timeoutMs: 12000
    property string pendingId: ""
    property double deadline: 0
    readonly property bool running: pendingId.length > 0
    signal discoveryRequested()
    signal found(string uuid)
    signal failed(bool ambiguous)

    function cancel() {
        pendingId = "";
        poll.stop();
    }

    function start(deviceId) {
        cancel();
        pendingId = deviceId;
        deadline = Date.now() + timeoutMs;
        check();
        if (running) {
            discoveryRequested();
            poll.start();
        }
    }

    function check() {
        if (!running)
            return;
        var index = model.findDevice(pendingId);
        if (index === -2) {
            cancel();
            failed(true);
        } else if (index >= 0 && model.isComputerOnline(index)) {
            var uuid = model.computerUuid(index);
            cancel();
            found(uuid);
        } else if (Date.now() >= deadline) {
            cancel();
            failed(false);
        }
    }

    property Timer poll: Timer {
        interval: 200
        repeat: true
        onTriggered: lookup.check()
    }
}
