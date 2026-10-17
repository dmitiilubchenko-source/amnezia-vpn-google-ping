#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include "core/repositories/secureServersRepository.h"
#include "secureQSettings.h"
#include "ui/controllers/api/premiumLatencyController.h"
#include "ui/controllers/api/serverLatencyProbe.h"
#include "ui/controllers/api/tunnelLatencyProbe.h"
#include "core/repositories/secureAppSettingsRepository.h"
#include "ui/controllers/connectionUiController.h"
#include "ui/controllers/api/amneziaBenchmarkBackend.h"

class TestPremiumLatency : public QObject
{
    Q_OBJECT

    static amnezia::ApiV2ServerConfig config(const QString &host)
    {
        QJsonObject json{{"configVersion", 2}, {"hostName", host}, {"defaultContainer", "amnezia-awg"},
            {"containers", QJsonArray{QJsonObject{{"container", "amnezia-awg"}, {"awg", QJsonObject{}}}}},
            {"api_config", QJsonObject{{"service_type", "amnezia-premium"}, {"service_protocol", "awg"},
                {"server_country_code", "de"}, {"available_countries", QJsonArray{
                    QJsonObject{{"server_country_code", "de"}}, QJsonObject{{"server_country_code", "nl"}}}}}}};
        return amnezia::ApiV2ServerConfig::fromJson(json);
    }

private slots:
    void freshProfileHasNoBundledSubscription()
    {
        SecureQSettings settings("AmneziaVPN-Test", QUuid::createUuid().toString(), nullptr, false);
        SecureServersRepository servers(&settings);
        SecureAppSettingsRepository appSettings(&settings);
        ConnectionUiController ui(nullptr, nullptr);
        AmneziaBenchmarkBackend backend(&servers, &appSettings, nullptr, &ui, nullptr, &settings, nullptr);
        PremiumBenchmarkController controller(&backend);
        QVERIFY(servers.orderedServerIds().isEmpty());
        QVERIFY(servers.defaultServerId().isEmpty());
        controller.refresh(servers.defaultServerId());
        QVERIFY(!controller.canStart());
        controller.startAll();
        QVERIFY(!controller.busy());
        QVERIFY(controller.ranking().isEmpty());
        settings.clearSettings();
    }

    void savedBenchmarkDataIsRemovedWithItsSubscription()
    {
        SecureQSettings settings("AmneziaVPN-Test", QUuid::createUuid().toString(), nullptr, false);
        SecureServersRepository servers(&settings);
        SecureAppSettingsRepository appSettings(&settings);
        ConnectionUiController ui(nullptr, nullptr);
        AmneziaBenchmarkBackend backend(&servers, &appSettings, nullptr, &ui, nullptr, &settings, nullptr);
        const auto id = servers.addServer({}, config("127.0.0.1").toJson(), serverConfigUtils::ConfigType::AmneziaPremiumV2);
        backend.savePreferences(id, {{"version", 1}, {"favorites", QStringList{"de"}}});
        QCOMPARE(backend.preferences(id).value("favorites").toStringList(), QStringList{"de"});
        servers.removeServer(id);
        QVERIFY(backend.preferences(id).isEmpty());
        backend.savePreferences(id, {{"version", 1}});
        QVERIFY(backend.preferences(id).isEmpty());
        settings.clearSettings();
    }

    void loopback_data()
    {
        QTest::addColumn<QString>("host");
        QTest::newRow("ipv4") << QString("127.0.0.1");
        QTest::newRow("ipv6") << QString("::1");
        QTest::newRow("dns") << QString("localhost");
    }

    void loopback()
    {
        QFETCH(QString, host);
        ServerLatencyProbe probe;
        QSignalSpy spy(&probe, &ServerLatencyProbe::finished);
        probe.start(host);
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 8000);
        const auto measurement = spy.at(0).at(0).toMap();
#ifdef Q_OS_WIN
        QCOMPARE(measurement.value("state").toString(), QString("ready"));
        QCOMPARE(measurement.value("replies").toInt(), 3);
        QVERIFY(measurement.value("latencyMs").toInt() >= 0);
#else
        QCOMPARE(measurement.value("state").toString(), QString("unsupported"));
#endif
    }

    void unresolvedHostDoesNotBecomeZeroPing()
    {
        ServerLatencyProbe probe;
        QSignalSpy spy(&probe, &ServerLatencyProbe::finished);
        probe.start("amnezia-ping-test.invalid");
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 6000);
        const auto measurement = spy.at(0).at(0).toMap();
        QCOMPARE(measurement.value("state").toString(), QString("dnsError"));
        QCOMPARE(measurement.value("latencyMs").toInt(), -1);
    }

    void deletingProbeCancelsDelivery()
    {
        QObject receiver;
        int deliveries = 0;
        auto *probe = new ServerLatencyProbe;
        connect(probe, &ServerLatencyProbe::finished, &receiver, [&deliveries]() { ++deliveries; });
        probe->start("127.0.0.1");
        delete probe;
        QTest::qWait(100);
        QCOMPARE(deliveries, 0);
    }

    void missingAddressDoesNotPingLocalhost()
    {
        ServerLatencyProbe probe;
        QSignalSpy spy(&probe, &ServerLatencyProbe::finished);
        probe.start({});
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 1000);
        const auto measurement = spy.at(0).at(0).toMap();
        QCOMPARE(measurement.value("state").toString(), QString("dnsError"));
        QCOMPARE(measurement.value("latencyMs").toInt(), -1);
    }

    void cacheIsolationExpiryAndConnectionGuard()
    {
        const QString org = "AmneziaVPN-Ping-Test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        SecureQSettings settings(org, "test", nullptr, false);
        SecureServersRepository servers(&settings);
        const auto first = config("127.0.0.1");
        const auto id1 = servers.addServer({}, first.toJson(), serverConfigUtils::ConfigType::AmneziaPremiumV2);
        const auto second = config("127.0.0.2");
        const auto id2 = servers.addServer({}, second.toJson(), serverConfigUtils::ConfigType::AmneziaPremiumV2);
        QVERIFY(!id1.isEmpty());
        QVERIFY(!id2.isEmpty());
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const QJsonObject cached{{id1, QJsonObject{{"nl", QJsonObject{
            {"host", "127.0.0.1"}, {"protocol", "awg"}, {"seen", now - 86401}}}}},
            {id2, QJsonObject{{"nl", QJsonObject{
            {"host", "127.0.0.2"}, {"protocol", "awg"}, {"seen", now}}}}}};
        settings.setValue("Conf/premiumLatencyTargets", QJsonDocument(cached).toJson());
        ConnectionUiController connection(nullptr, nullptr);
        // No real VPN or gateway is used in this fixture.
        PremiumLatencyController controller(&servers, &settings, &connection);
        SecureAppSettingsRepository appSettings(&settings);
        controller.setAppSettings(&appSettings);
        controller.refresh(id1);
        QVERIFY(!controller.tunnelAvailable());
        controller.checkConnected(id1);
        QVERIFY(!controller.busy());
        QVERIFY(controller.tunnelResults().isEmpty());
        QCOMPARE(controller.results().value("de").toMap().value("state").toString(), QString("idle"));
        QCOMPARE(controller.results().value("nl").toMap().value("state").toString(), QString("unknown"));
        controller.refresh(id2);
        QCOMPARE(controller.results().value("nl").toMap().value("state").toString(), QString("idle"));

#ifdef Q_OS_WIN
        controller.checkAll(id2);
        QVERIFY(controller.busy());
        controller.refresh(id1);
        QVERIFY(!controller.busy());
        QTest::qWait(100);
        QCOMPARE(controller.results().value("de").toMap().value("state").toString(), QString("idle"));
#endif
        controller.refresh(id2);
        auto changed = second;
        changed.apiConfig.serviceProtocol = "vless";
        servers.editServer(id2, changed.toJson(), serverConfigUtils::ConfigType::AmneziaPremiumV2);
        QCOMPARE(controller.results().value("nl").toMap().value("state").toString(), QString("unknown"));

        // Preparing a connection cancels probes and blocks new measurements.
        connection.onConnectionStateChanged(Vpn::ConnectionState::Preparing);
        QVERIFY(!controller.available());
        QVERIFY(!controller.tunnelAvailable());
        controller.checkConnected(id2);
        QVERIFY(!controller.busy());
        controller.checkAll(id2);
        QVERIFY(!controller.busy());
        controller.clearCache();
        QCOMPARE(controller.results().value("de").toMap().value("state").toString(), QString("unknown"));
        servers.removeServer(id2);
        const auto savedCache = QJsonDocument::fromJson(settings.value("Conf/premiumLatencyTargets").toByteArray()).object();
        QVERIFY(!savedCache.contains(id2));
        QVERIFY(savedCache.contains(id1));
        settings.clearSettings();
    }
};

QTEST_GUILESS_MAIN(TestPremiumLatency)
#include "testPremiumLatency.moc"
