import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    modal: true
    anchors.centerIn: Overlay.overlay
    standardButtons: DialogButtonBox.Cancel | DialogButtonBox.Ok

    property string mode: "string"
    property alias text: label.text
    property string unit

    property alias textValue: text.text

    property alias intValue: intSpin.value
    property alias intMinimum: intSpin.from
    property alias intMaximum: intSpin.to

    property double doubleValue
    property double doubleMinimum
    property double doubleMaximum
    property int doubleDecimals

    onDoubleValueChanged: dblSpin.value = doubleValue * dblSpin.factor

    ColumnLayout {
        Label {
            id: label
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
        }
        TextField {
            id: text
            visible: mode === "string"
            Layout.fillWidth: true
        }
        RowLayout {
            SpinBox {
                id: intSpin
                visible: mode === "int"
                editable: true
                from: intValidator.bottom
                to: intValidator.top
                validator: IntValidator { id: intValidator }
                Layout.fillWidth: true
            }
            Label {
                text: root.unit
                visible: intSpin.visible && text !== ''
            }
        }
        RowLayout {
            SpinBox {
                id: dblSpin
                visible: mode === "double"
                editable: true

                property int factor: Math.pow(10, root.doubleDecimals)
                property double readDoubleValue: value / factor

                onReadDoubleValueChanged: root.doubleValue = readDoubleValue

                from: root.doubleMinimum * factor
                to: root.doubleMaximum * factor

                validator: DoubleValidator {
                    id: dblValidator
                    bottom: Math.min(dblSpin.from, dblSpin.to)
                    top:  Math.max(dblSpin.from, dblSpin.to)
                }
                textFromValue: function(value, locale) {
                    return Number(value / factor).toLocaleString(locale, 'f', root.doubleDecimals)
                }

                valueFromText: function(text, locale) {
                    return Number.fromLocaleString(locale, text) * factor
                }
                Layout.fillWidth: true
            }
            Label {
                text: root.unit
                visible: dblSpin.visible && text !== ''
            }
        }
    }
}
