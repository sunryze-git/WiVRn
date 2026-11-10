import QtQuick
import org.kde.kirigami as Kirigami
import org.wyrmvr.wyrm

Kirigami.MenuDialog {
    id: select_usb_device

    property int connected_headset_count: 0
    property string connected_headset_serial: ""

    title: i18n("Select your headset")
    showCloseButton: true

    Connections {
        target: Adb

        function onRowsInserted() {
            select_usb_device.connected_devices_changed();
        }

        function onRowsRemoved() {
            select_usb_device.connected_devices_changed();
        }

        function onDataChanged() {
            select_usb_device.connected_devices_changed();
        }
    }

    Component {
        id: usb_device_component
        Kirigami.Action {
            property string serial
            property string label

            text: label
            onTriggered: Adb.startUsbConnection(serial, WyrmServer.pin)
        }
    }

    Component.onCompleted: select_usb_device.connected_devices_changed()

    actions: []

    function connect() {
        if (select_usb_device.connected_headset_count == 1) {
            Adb.startUsbConnection(select_usb_device.connected_headset_serial, WyrmServer.pin);
        } else {
            select_usb_device.open();
        }
    }

    function connected_devices_changed() {
        var n = Adb.rowCount();
        var nb_found = 0;

        select_usb_device.actions.length = 0;

        for (var i = 0; i < n; i++) {
            var serial = Adb.data(Adb.index(i, 0), 257);
            var isWyrmInstalled = Adb.data(Adb.index(i, 0), 258);
            var manufacturer = Adb.data(Adb.index(i, 0), 259);
            var model = Adb.data(Adb.index(i, 0), 260);

            if (isWyrmInstalled) {
                nb_found++;
                select_usb_device.connected_headset_serial = serial;

                select_usb_device.actions.push(usb_device_component.createObject(select_usb_device, {
                    "label": manufacturer + " " + model,
                    "serial": serial
                }));
            }
        }

        select_usb_device.connected_headset_count = nb_found;
        if (nb_found != 1)
            select_usb_device.connected_headset_serial = "";

        if (DashboardSettings.auto_connect_usb && nb_found == 1 && !WyrmServer.headsetConnected) {
            Adb.startUsbConnection(select_usb_device.connected_headset_serial, WyrmServer.pin);
        }
    }
}
