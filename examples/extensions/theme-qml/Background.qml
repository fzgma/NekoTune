import QtQuick
import NekoTune 1.0
Rectangle {
    objectName: "themeExampleBackground"
    ExtensionApi { id: api }
    gradient: Gradient {
        GradientStop { position: 0; color: api.theme.bgCanvas }
        GradientStop { position: 1; color: api.theme.accentGlow }
    }
}
