import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import SortFilterProxyModel 0.2

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    property var processedServer
    property bool subscriptionExpired: false
    property bool subscriptionExpiringSoon: false
    property bool isSubscriptionRenewalAvailable: false
    property bool isInAppPurchase: false
    property bool favoritesOnly: false
    property string confirmedAction: "all"
    property string confirmedCountry: ""
    property string confirmedCountryName: ""

    function confirmTest(action, code, name) {
        root.confirmedAction = action
        root.confirmedCountry = code || ""
        root.confirmedCountryName = name || ""
        benchmarkConfirmation.open()
    }

    function dateText(value) {
        return value ? Qt.formatDateTime(new Date(value), "dd.MM HH:mm") : ""
    }

    function dateLabel(measurement) {
        if (!measurement || !measurement.checkedAtUtc) return ""
        return (measurement.stale ? qsTr("Outdated: %1") : measurement.historical ? qsTr("Earlier check: %1")
            : measurement.saved ? qsTr("Saved: %1") : qsTr("Checked: %1")).arg(dateText(measurement.checkedAtUtc))
    }

    function recommendationText() {
        const result = BenchmarkController.recommendation
        if (!result || !result.countryCode) return ""
        const label = result.saved ? qsTr("Saved leader: %1") : qsTr("Best in the latest check: %1")
        return label.arg(result.countryName) + "\n"
            + qsTr("Median %1 · P95 %2 · loss %3%").arg(timeText(result.medianMs)).arg(timeText(result.p95Ms)).arg(numberText(result.loss))
            + (result.saved ? "\n" + qsTr("%1 · recheck if your network changed").arg(dateText(result.checkedAtUtc)) : "")
    }

    function numberText(value) {
        return Number(value).toFixed(1).replace(/\.0$/, "")
    }

    function timeText(value) {
        if (value === undefined || value < 0) return "—"
        return value === 0 ? qsTr("<1 ms") : qsTr("%1 ms").arg(numberText(value))
    }

    function serverPingText(code) {
        const result = PremiumLatencyController.results[code]
        if (!result || result.state === "unknown" || result.state === "idle") return qsTr("Server ping: —")
        if (result.state === "checking") return qsTr("Server ping: checking...")
        if (result.state === "ready") return qsTr("Server ping: %1").arg(timeText(result.latencyMs))
        if (result.state === "timeout") return qsTr("Server ping: no ICMP reply")
        if (result.state === "dnsError") return qsTr("Could not resolve server address")
        if (result.errorCode) return qsTr("Server ping: Windows error %1").arg(result.errorCode)
        return qsTr("Server ping: error")
    }

    function latencyText(measurement) {
        const state = measurement ? measurement.state : "unknown"
        if (state === "ready") {
            const label = measurement.measurementStage === "detailed" ? qsTr("Detailed") : qsTr("Quick estimate")
            return qsTr("Google: median %1 · P95 %2").arg(timeText(measurement.medianMs)).arg(timeText(measurement.p95Ms))
                + "\n" + qsTr("Variation %1 · loss %2% (%3/%4)").arg(timeText(measurement.jitterMs))
                    .arg(numberText(measurement.loss)).arg(measurement.replies).arg(measurement.sent)
                + "\n" + qsTr("%1 · %2 requests · %3").arg(label).arg(measurement.sent).arg(measurement.protocol || "")
                + (measurement.replies < 3 ? "\n" + qsTr("Few replies: low confidence") : "")
                + (measurement.checkedAtUtc ? "\n" + dateLabel(measurement) : "")
        }
        if (state === "checking") return qsTr("Checking ping...")
        const stamp = measurement && measurement.checkedAtUtc ? "\n" + dateLabel(measurement) : ""
        if (state === "timeout") return qsTr("No Google ping reply") + stamp
        if (state === "connectionError") return qsTr("VPN connection failed") + stamp
        if (state === "apiError") return qsTr("Could not load this location") + stamp
        if (state === "routeError") return qsTr("Could not verify the VPN route") + stamp
        if (state === "tlsError") return qsTr("TLS verification failed")
        if (state === "networkError" || state === "httpError") return qsTr("HTTPS test failed")
        if (state === "dnsError") return qsTr("Could not resolve server address")
        if (state === "error" || state === "pingError") return qsTr("Ping measurement failed")
        return qsTr("Not measured — use Check all locations")
    }

    function updateSubscriptionState() {
        root.subscriptionExpired = ServersUiController.isServerSubscriptionExpired(ServersUiController.processedServerId)
        root.subscriptionExpiringSoon = ServersUiController.isServerSubscriptionExpiringSoon(ServersUiController.processedServerId)
        root.isSubscriptionRenewalAvailable = ApiAccountInfoModel.data("isSubscriptionRenewalAvailable")
        root.isInAppPurchase = ApiAccountInfoModel.data("isInAppPurchase")
    }

    function selectConnectionCountry(countryIndex, countryCode, countryName) {
        if ((PremiumLatencyController.isPremium && countryCode === BenchmarkController.currentCountry)
            || (!PremiumLatencyController.isPremium && countryIndex === ApiCountryModel.currentIndex)) {
            return
        }

        PageController.showBusyIndicator(true)
        SubscriptionUiController.updateServiceFromGateway(ServersUiController.processedServerId, countryCode, countryName)
        PageController.showBusyIndicator(false)
    }

    Component.onCompleted: {
        root.updateSubscriptionState()
        PremiumLatencyController.refresh(ServersUiController.processedServerId)
        BenchmarkController.refresh(ServersUiController.processedServerId)
    }

    Component.onDestruction: { PremiumLatencyController.cancel(); BenchmarkController.stop() }

    onVisibleChanged: {
        if (!visible) { PremiumLatencyController.cancel(); BenchmarkController.stop() }
    }

    Connections {
        target: ServersUiController

        function onProcessedServerIdChanged() {
            root.processedServer = proxyServersModel.get(0)
            root.updateSubscriptionState()
            PremiumLatencyController.refresh(ServersUiController.processedServerId)
            BenchmarkController.refresh(ServersUiController.processedServerId)
        }
    }

    Dialog {
        id: benchmarkConfirmation
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 420)
        modal: true
        padding: 16
        background: Rectangle {
            color: AmneziaStyle.color.midnightBlack
            border.color: AmneziaStyle.color.mutedGray
            radius: 16
        }
        contentItem: ColumnLayout {
            spacing: 12
            ParagraphTextType {
                Layout.fillWidth: true
                font.weight: 600
                text: qsTr("Check all locations?")
            }
            ParagraphTextType {
                Layout.fillWidth: true
                text: root.confirmedAction === "favorites"
                    ? qsTr("Check %1 favorite locations with 30 requests each? The VPN will reconnect; internet and chat may disconnect. The test attempts to restore the original connection.").arg(BenchmarkController.favoriteCount)
                    : root.confirmedAction === "country"
                    ? qsTr("Check %1 with 30 requests? The VPN will reconnect; internet and chat may disconnect. The test attempts to restore the original connection.").arg(root.confirmedCountryName)
                    : qsTr("Two stages: 5 Google pings per location, then 30 pings for the best 5. The VPN will reconnect; internet and this chat may disconnect. The test attempts to restore the original connection. You can stop it.")
            }
            BasicButtonType {
                Layout.fillWidth: true
                text: qsTr("Start test")
                clickedFunc: function() { benchmarkConfirmation.accept() }
            }
            BasicButtonType {
                Layout.fillWidth: true
                text: qsTr("Cancel")
                clickedFunc: function() { benchmarkConfirmation.reject() }
            }
        }
        onAccepted: {
            if (root.confirmedAction === "favorites") BenchmarkController.startFavorites()
            else if (root.confirmedAction === "country") BenchmarkController.checkCountry(root.confirmedCountry)
            else BenchmarkController.startAll()
        }
    }

    Connections {
        target: ServersModel

        function onModelReset() {
            root.processedServer = proxyServersModel.get(0)
        }
    }

    Connections {
        target: ApiAccountInfoModel

        function onModelReset() {
            root.updateSubscriptionState()
        }
    }

    SortFilterProxyModel {
        id: proxyServersModel
        objectName: "proxyServersModel"

        sourceModel: ServersModel
        filters: [
            ValueFilter {
                roleName: "serverId"
                value: ServersUiController.processedServerId
            }
        ]

        Component.onCompleted: {
            root.processedServer = proxyServersModel.get(0)
        }
    }

    ListViewType {
        id: menuContent

        anchors.fill: parent

        model: PremiumLatencyController.isPremium ? BenchmarkController.ranking.filter(function(row) {
            return !root.favoritesOnly || row.favorite
        }) : ApiCountryModel

        currentIndex: PremiumLatencyController.isPremium ? -1 : ApiCountryModel.currentIndex

        ButtonGroup {
            id: containersRadioButtonGroup
        }

        header: ColumnLayout {
            width: menuContent.width

            spacing: 4

            BackButtonType {
                id: backButton
                objectName: "backButton"

                Layout.topMargin: 20 + PageController.safeAreaTopMargin
            }

            HeaderTypeWithButton {
                id: headerContent
                objectName: "headerContent"

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: root.subscriptionExpired || root.subscriptionExpiringSoon ? 0 : 4

                actionButtonImage: "qrc:/images/controls/settings.svg"

                headerText: PremiumLatencyController.isPremium ? "Google Ping & Rank" : root.processedServer.name

                actionButtonFunction: function() {
                    PageController.showBusyIndicator(true)
                    let result = SubscriptionUiController.getAccountInfo(ServersUiController.processedServerId, false)
                    PageController.showBusyIndicator(false)
                    if (!result) {
                        return
                    }

                    PageController.goToPage(PageEnum.PageSettingsApiServerInfo)
                }
            }

            ParagraphTextType {
                visible: root.subscriptionExpired || root.subscriptionExpiringSoon

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 12

                text: root.subscriptionExpired ? qsTr("Subscription expired") : qsTr("Subscription expiring soon")
                color: root.subscriptionExpired ? AmneziaStyle.color.vibrantRed : AmneziaStyle.color.goldenApricot
            }

            BasicButtonType {
                visible: (root.subscriptionExpired || root.subscriptionExpiringSoon)
                    && root.isSubscriptionRenewalAvailable && !root.isInAppPurchase

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 28
                Layout.bottomMargin: 0

                defaultColor: AmneziaStyle.color.paleGray
                hoveredColor: AmneziaStyle.color.lightGray
                pressedColor: AmneziaStyle.color.mutedGray
                textColor: AmneziaStyle.color.midnightBlack

                text: qsTr("Renew subscription")

                clickedFunc: function() {
                    SubscriptionUiController.getRenewalLink(ServersUiController.processedServerId)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: (root.subscriptionExpired || root.subscriptionExpiringSoon) ? 12 : 4
                Layout.bottomMargin: 8
                ParagraphTextType {
                    Layout.fillWidth: true
                    text: qsTr("Location for connection")
                    color: AmneziaStyle.color.mutedGray
                }
                ToolButton {
                    id: pingAllServersButton
                    objectName: "pingAllServersButton"
                    visible: PremiumLatencyController.supported && PremiumLatencyController.isPremium
                    enabled: !BenchmarkController.busy && (PremiumLatencyController.available || PremiumLatencyController.busy)
                    implicitWidth: 40; implicitHeight: 40
                    Accessible.name: PremiumLatencyController.busy ? qsTr("Stop server ping") : qsTr("Ping all servers · 3 requests")
                    ToolTip.visible: hovered
                    ToolTip.text: Accessible.name + "\n" + qsTr("Current route. No VPN switching. Unknown address: —")
                    background: Rectangle { radius: 8; color: pingAllServersButton.hovered ? AmneziaStyle.color.charcoalGray : "transparent" }
                    contentItem: Item {
                        Image {
                            anchors.centerIn: parent
                            visible: !PremiumLatencyController.busy
                            source: "qrc:/images/controls/gauge.svg"
                            sourceSize.width: 24; sourceSize.height: 24
                            opacity: pingAllServersButton.enabled ? 1 : 0.35
                        }
                        BusyIndicator {
                            anchors.centerIn: parent
                            width: 24; height: 24
                            running: PremiumLatencyController.busy
                            visible: running
                        }
                    }
                    onClicked: {
                        if (PremiumLatencyController.busy) PremiumLatencyController.cancel()
                        else PremiumLatencyController.checkAll(ServersUiController.processedServerId)
                    }
                }
            }

            BasicButtonType {
                objectName: "checkPremiumPingButton"
                visible: PremiumLatencyController.supported && PremiumLatencyController.isPremium
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                enabled: !PremiumLatencyController.busy && (BenchmarkController.canStart || BenchmarkController.busy)
                text: BenchmarkController.busy ? qsTr("Stop test") : qsTr("Check all locations · Google")
                clickedFunc: function() {
                    if (BenchmarkController.busy) BenchmarkController.stop()
                    else root.confirmTest("all")
                }
            }

            RowLayout {
                visible: PremiumLatencyController.supported && PremiumLatencyController.isPremium
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                spacing: 8
                BasicButtonType {
                    objectName: "checkFavoritesButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    enabled: !PremiumLatencyController.busy && BenchmarkController.canStart && BenchmarkController.favoriteCount > 0
                    buttonTextLabel.font.pixelSize: 14
                    text: qsTr("Favorites (%1)").arg(BenchmarkController.favoriteCount)
                    clickedFunc: function() { root.confirmTest("favorites") }
                }
                BasicButtonType {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    enabled: !PremiumLatencyController.busy && BenchmarkController.canMeasure
                    buttonTextLabel.font.pixelSize: 14
                    text: qsTr("Current · 30 pings")
                    clickedFunc: function() { BenchmarkController.checkCurrent() }
                }
            }

            CheckBox {
                objectName: "favoritesOnlyCheckBox"
                visible: PremiumLatencyController.supported && PremiumLatencyController.isPremium
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                enabled: !BenchmarkController.busy
                checked: root.favoritesOnly
                text: qsTr("Show favorites only")
                palette.windowText: AmneziaStyle.color.paleGray
                font.pixelSize: 14
                onToggled: root.favoritesOnly = checked
            }

            CaptionTextType {
                visible: PremiumLatencyController.isPremium && root.recommendationText() !== ""
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 4
                color: AmneziaStyle.color.goldenApricot
                text: root.recommendationText()
            }

            ProgressBar {
                id: overallBar
                visible: PremiumLatencyController.isPremium && BenchmarkController.busy
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                padding: 0
                value: BenchmarkController.overallProgress
                background: Rectangle { implicitHeight: 6; radius: 3; color: AmneziaStyle.color.charcoalGray }
                contentItem: Item {
                    implicitHeight: 6
                    Rectangle { width: parent.width * overallBar.visualPosition; height: parent.height; radius: 3; color: AmneziaStyle.color.goldenApricot }
                }
            }

            CaptionTextType {
                visible: PremiumLatencyController.isPremium && BenchmarkController.busy
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                text: BenchmarkController.progressText
                color: AmneziaStyle.color.mutedGray
            }

            CaptionTextType {
                visible: PremiumLatencyController.isPremium && BenchmarkController.status !== ""
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                color: AmneziaStyle.color.goldenApricot
                text: BenchmarkController.status
            }

            CaptionTextType {
                visible: PremiumLatencyController.isPremium && PremiumLatencyController.status !== ""
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                text: PremiumLatencyController.status
                color: AmneziaStyle.color.paleGray
            }

            CaptionTextType {
                visible: PremiumLatencyController.supported && PremiumLatencyController.isPremium
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                Layout.bottomMargin: 12
                color: AmneziaStyle.color.mutedGray
                text: qsTr("v5.2 · Speedometer: ping known servers without reconnecting. Stars: favorites. Arrows: detailed checks.")
            }
        }

        delegate: ColumnLayout {
            id: content
            property string locationCode: PremiumLatencyController.isPremium ? modelData.countryCode : countryCode
            property string locationName: PremiumLatencyController.isPremium ? modelData.countryName : countryName
            property string flagCode: PremiumLatencyController.isPremium ? modelData.countryImageCode : countryImageCode

            width: menuContent.width
            height: content.implicitHeight

            RowLayout {
                VerticalRadioButton {
                    id: containerRadioButton

                    Layout.fillWidth: true
                    Layout.leftMargin: 16

                    text: content.locationName
                    descriptionText: PremiumLatencyController.supported && PremiumLatencyController.isPremium
                        ? root.serverPingText(content.locationCode) + (modelData.state ? "\n" + root.latencyText(modelData) : "") : ""

                    ButtonGroup.group: containersRadioButtonGroup

                    imageSource: "qrc:/images/controls/download.svg"

                    checked: PremiumLatencyController.isPremium ? content.locationCode === BenchmarkController.currentCountry : index === ApiCountryModel.currentIndex
                    checkable: !ConnectionController.isConnected && !BenchmarkController.busy
                    enabled: !BenchmarkController.busy

                    onClicked: {
                        if (ConnectionController.isConnectionInProgress) {
                            PageController.showNotificationMessage(qsTr("Unable change server location while trying to make an active connection"))
                            return
                        }
                        if (ConnectionController.isConnected) {
                            PageController.showNotificationMessage(qsTr("Unable change server location while there is an active connection"))
                            return
                        }

                        root.selectConnectionCountry(index, content.locationCode, content.locationName)
                    }

                    Keys.onEnterPressed: {
                        if (checkable) {
                            checked = true
                        }
                        containerRadioButton.clicked()
                    }
                    Keys.onReturnPressed: {
                        if (checkable) {
                            checked = true
                        }
                        containerRadioButton.clicked()
                    }
                }

                ColumnLayout {
                    Layout.rightMargin: 16
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 2
                    Image {
                        Layout.alignment: Qt.AlignHCenter
                        source: content.flagCode !== "" ? "qrc:/countriesFlags/images/flagKit/" + content.flagCode + ".svg" : ""
                    }
                    ToolButton {
                        id: favoriteButton
                        objectName: "favorite-" + content.locationCode
                        visible: PremiumLatencyController.supported && PremiumLatencyController.isPremium
                        enabled: !BenchmarkController.busy
                        implicitWidth: 36; implicitHeight: 32
                        Accessible.name: PremiumLatencyController.isPremium && modelData.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                        ToolTip.visible: hovered
                        ToolTip.text: Accessible.name
                        background: Rectangle { radius: 8; color: favoriteButton.hovered ? AmneziaStyle.color.charcoalGray : "transparent" }
                        contentItem: Image {
                            source: PremiumLatencyController.isPremium && modelData.favorite
                                ? "qrc:/images/controls/star-filled.svg" : "qrc:/images/controls/star.svg"
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 22; sourceSize.height: 22
                            opacity: favoriteButton.enabled ? 1 : 0.35
                        }
                        onClicked: BenchmarkController.toggleFavorite(content.locationCode)
                    }
                    ToolButton {
                        id: recheckButton
                        objectName: "recheck-" + content.locationCode
                        visible: PremiumLatencyController.supported && PremiumLatencyController.isPremium
                        enabled: !PremiumLatencyController.busy && BenchmarkController.canStart
                        implicitWidth: 36; implicitHeight: 32
                        Accessible.name: qsTr("Recheck this location with 30 pings")
                        ToolTip.visible: hovered
                        ToolTip.text: Accessible.name
                        background: Rectangle { radius: 8; color: recheckButton.hovered ? AmneziaStyle.color.charcoalGray : "transparent" }
                        contentItem: Image {
                            source: "qrc:/images/controls/refresh-cw.svg"
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 20; sourceSize.height: 20
                            opacity: recheckButton.enabled ? 1 : 0.25
                        }
                        onClicked: {
                            if (BenchmarkController.canMeasure && content.locationCode === BenchmarkController.currentCountry) BenchmarkController.checkCurrent()
                            else root.confirmTest("country", content.locationCode, content.locationName)
                        }
                    }
                }
            }

            DividerType {
                Layout.fillWidth: true
            }
        }
    }
}
