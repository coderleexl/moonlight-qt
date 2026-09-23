import QtQuick 2.9
import QtTest 1.2
import "../../app/gui" as Desk

TestCase {
    name: "DeviceIdLookup"
    when: windowShown
    QtObject {
        id: computers
        property var devices: []
        function findDevice(identifier) {
            var result = -1;
            for (var i = 0; i < devices.length; ++i) {
                if (devices[i].code === identifier || devices[i].uuid === identifier) {
                    if (result >= 0) return -2;
                    result = i;
                }
            }
            return result;
        }
        function computerUuid(index) { return devices[index].uuid; }
        function isComputerOnline(index) { return devices[index].online; }
    }
    Desk.DeviceIdLookup { id: lookup; model: computers }
    SignalSpy { id: found; target: lookup; signalName: "found" }
    SignalSpy { id: failed; target: lookup; signalName: "failed" }
    SignalSpy { id: discovery; target: lookup; signalName: "discoveryRequested" }
    function init() {
        lookup.cancel();
        lookup.timeoutMs = 500;
        computers.devices = [];
        found.clear(); failed.clear(); discovery.clear();
    }
    function cleanup() { lookup.cancel(); }
    function host(code, uuid, online) { return {code: code, uuid: uuid, online: online}; }
    function test_known_device() {
        computers.devices = [host("123456789", "one", true)];
        lookup.start("123456789");
        compare(found.count, 1);
        compare(found.signalArguments[0][0], "one");
        compare(discovery.count, 0);
    }
    function test_discovered_after_submission() {
        lookup.start("123456789");
        compare(lookup.running, true);
        compare(discovery.count, 1);
        wait(50);
        computers.devices = [host("987654321", "wrong", true), host("123456789", "right", true)];
        tryCompare(found, "count", 1);
        compare(found.signalArguments[0][0], "right");
        compare(failed.count, 0);
        compare(lookup.running, false);
    }
    function test_wait_for_saved_offline_device() {
        computers.devices = [host("123456789", "one", false)];
        lookup.start("123456789");
        compare(found.count, 0);
        computers.devices = [host("123456789", "one", true)];
        tryCompare(found, "count", 1);
    }
    function test_unknown_device_times_out() {
        lookup.start("123456789");
        tryCompare(failed, "count", 1);
        compare(failed.signalArguments[0][0], false);
        compare(found.count, 0);
        compare(lookup.running, false);
    }
    function test_cancel_prevents_late_password_prompt() {
        lookup.start("123456789");
        lookup.cancel();
        computers.devices = [host("123456789", "one", true)];
        wait(650);
        compare(found.count, 0);
        compare(failed.count, 0);
    }
    function test_ambiguous_id_never_selects_first_device() {
        lookup.start("123456789");
        computers.devices = [host("123456789", "one", true), host("123456789", "two", true)];
        tryCompare(failed, "count", 1);
        compare(failed.signalArguments[0][0], true);
        compare(found.count, 0);
    }
    function test_new_request_replaces_old_request() {
        lookup.start("123456789");
        lookup.start("987654321");
        computers.devices = [host("123456789", "old", true)];
        wait(220);
        compare(found.count, 0);
        computers.devices = [host("987654321", "new", true)];
        tryCompare(found, "count", 1);
        compare(found.signalArguments[0][0], "new");
    }
}
