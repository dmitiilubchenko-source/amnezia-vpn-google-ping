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

    function numberText(value) {
        return Number(value).toFixed(1).replace(/\.0$/, "")
    }

    function timeText(value) {
        if (value === undefined || value < 0) return "—"
        return value === 0 ? qsTr("<1 ms") : qsTr("%1 ms").arg(numberText(value))
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
        }
        if (state === "checking") return qsTr("Checking ping...")
        if (state === "timeout") return qsTr("No Google ping reply")
        if (state === "connectionError") return qsTr("VPN connection failed")
        if (state === "apiError") return qsTr("Could not load this location")
        if (state === "routeError") return qsTr("Could not verify the VPN route")
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
                text: qsTr("Two stages: 5 Google pings per location, then 30 pings for the best 5. The VPN will reconnect; internet and this chat may disconnect. The test attempts to restore the original connection. You can stop it.")
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
        onAccepted: BenchmarkController.startAll()
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

        model: PremiumLatencyController.isPremium ? BenchmarkController.ranking : ApiCountryModel

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

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: (root.subscriptionExpired || root.subscriptionExpiringSoon) ? 12 : 4
                Layout.bottomMargin: 8

                text: qsTr("Location for connection")
                color: AmneziaStyle.color.mutedGray
            }

            BasicButtonType {
                objectName: "checkPremiumPingButton"
                visible: PremiumLatencyController.supported && PremiumLatencyController.isPremium
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                enabled: BenchmarkController.canStart || BenchmarkController.busy
                text: BenchmarkController.busy ? qsTr("Stop test") : qsTr("Check all locations · Google")
                clickedFunc: function() {
                    if (BenchmarkController.busy) BenchmarkController.stop()
                    else benchmarkConfirmation.open()
                }
            }

            BasicButtonType {
                visible: PremiumLatencyController.supported && PremiumLatencyController.isPremium
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                enabled: BenchmarkController.canMeasure
                text: qsTr("Current location · 30 pings")
                clickedFunc: function() { BenchmarkController.checkCurrent() }
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
                visible: PremiumLatencyController.supported && PremiumLatencyController.isPremium
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                Layout.bottomMargin: 12
                color: AmneziaStyle.color.mutedGray
                text: qsTr("v4 · Detailed results first; then quick estimates. Sorted by loss, P95, median and variation. P95: 95% of replies were no slower. App split tunneling must be off; Google must use VPN.")
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
                        ? root.latencyText(modelData) : ""

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

                Image {
                    Layout.rightMargin: 32
                    Layout.alignment: Qt.AlignRight

                    source: content.flagCode !== "" ? "qrc:/countriesFlags/images/flagKit/" + content.flagCode + ".svg" : ""
                }
            }

            DividerType {
                Layout.fillWidth: true
            }
        }
    }
}
