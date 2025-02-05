import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    property alias text: progressLabel.text
    property alias done: progress.value
    property alias total: progress.to

    signal requestCancel()

    modal: true
    standardButtons: DialogButtonBox.Cancel
    closePolicy: Popup.NoAutoClose // we can't just let the user close this dialog
    anchors.centerIn: Overlay.overlay

    ColumnLayout {
        Label { id: progressLabel }
        ProgressBar {
            id: progress
            indeterminate: !to && !value
        }
    }
    footer: RowLayout {
        // we need to hide the box inside a layout to prevent the automatically
        // created connections to Dialog.accept and .reject
        DialogButtonBox {
            standardButtons: root.standardButtons
            Layout.fillWidth: true

            onAccepted: root.accept()
            onRejected: root.requestCancel()
        }
    }
}
