import QtQuick
import QtQuick.Controls
import NekoTune 1.0

Item {
    ExtensionApi { id: api }
    Column {
        anchors.centerIn: parent
        spacing: 12
        Label { id: message; text: "Hello / 你好" }
        Button {
            text: "Say hello / 打招呼"
            onClicked: api.invoke("hello", { name: "NekoTune" })
                .then(result => message.text = result.message)
                .catch(error => message.text = String(error))
        }
    }
}
