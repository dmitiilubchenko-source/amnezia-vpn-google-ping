#include <QTest>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include "ui/controllers/api/premiumBenchmarkController.h"
#include "ui/controllers/api/pingStatistics.h"

class FakeBackend final : public BenchmarkBackend {
public:
    State connection = Connected;
    QString selected = "a", proto = "awg", failConnection;
    bool allowed = true, delayLoad = false;
    QStringList loads, protocolPreferences;
    QStringList codes{"a", "b", "c"};
    int connects = 0, disconnects = 0;
    std::function<void()> pending;
    QMap<QString, QVariantMap> saved;
    QVariantMap preferences(const QString &id) const override { return saved.value(id); }
    void savePreferences(const QString &id, const QVariantMap &value) override {
        saved[id] = QJsonDocument::fromVariant(value).object().toVariantMap();
    }
    bool eligible(const QString &id) const override { return allowed && id == "premium"; }
    State state() const override { return connection; }
    QVariantList countries(const QString &) const override {
        QVariantList rows;
        for (const auto &code : codes) rows.append(QVariantMap{{"countryCode", code}, {"countryName", code.toUpper()}});
        return rows;
    }
    QString country(const QString &) const override { return selected; }
    QString protocol(const QString &) const override { return proto; }
    void resolveReference(quint64 requestId, std::shared_ptr<std::atomic_bool> cancelled) override {
        QTimer::singleShot(0, this, [this, cancelled, requestId]() {
            if (!cancelled->load()) emit referenceResolved(requestId, QHostAddress("8.8.8.8"));
        });
    }
    void load(const QString &, const QString &code, const QString &preferred, quint64 requestId, std::shared_ptr<std::atomic_bool> cancelled) override {
        loads.append(code); protocolPreferences.append(preferred);
        auto complete = [this, code, preferred, cancelled, requestId]() {
            if (!cancelled->load()) { selected = code; if (!preferred.isEmpty()) proto = preferred; }
            emit loaded(requestId, !cancelled->load(), false);
        };
        if (delayLoad) pending = complete; else QTimer::singleShot(0, this, complete);
    }
    void connectVpn() override { ++connects; connection = selected == failConnection ? Error : Connected; emit changed(); }
    void disconnectVpn() override { ++disconnects; connection = Disconnected; emit changed(); }
    void completePending() { const auto callback = pending; pending = {}; if (callback) callback(); }
};

class FakeProbe final : public BenchmarkProbe {
public:
    FakeProbe(QVector<int> pattern, QString code, QList<QHostAddress> *references, QVariantList *trace, bool holdDetailed, QObject *parent)
        : BenchmarkProbe(parent), pattern(pattern), code(code), references(references), trace(trace), holdDetailed(holdDetailed) {}
    QVector<int> pattern;
    QString code;
    QList<QHostAddress> *references;
    QVariantList *trace;
    bool holdDetailed;
    void start(const QHostAddress &address, const PingSettings &settings) override {
        references->append(address);
        trace->append(QVariantMap{{"country", code}, {"count", settings.count}, {"interval", settings.intervalMs}});
        if (holdDetailed && settings.count == 30) return;
        QVector<int> observations;
        for (int i = 0; i < settings.count; ++i) observations.append(pattern[i % pattern.size()]);
        const auto result = PingStatistics::summarize(observations);
        QTimer::singleShot(0, this, [this, result]() { emit finished(result); });
    }
};

class TestPremiumBenchmark : public QObject {
    Q_OBJECT
private:
    QList<QHostAddress> references;
    QVariantList trace;
    QMap<QString, QVector<int>> patterns;
    bool holdDetailed = false;
    PremiumBenchmarkController::ProbeFactory factory(FakeBackend &backend) {
        return [this, &backend](QObject *parent) {
            const auto pattern = patterns.value(backend.selected, backend.selected == "b" ? QVector<int>{-1}
                : QVector<int>{backend.selected == "c" ? 10 : 30});
            return new FakeProbe(pattern, backend.selected, &references, &trace, holdDetailed, parent);
        };
    }
private slots:
    void init() { references.clear(); trace.clear(); patterns.clear(); holdDetailed = false; }
    void allLocationsSortAndRestoreConnected() {
        FakeBackend backend;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium");
        controller.startAll();
        QVERIFY(controller.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
        QCOMPARE(backend.loads, QStringList({"a", "b", "c", "c", "a", "a"}));
        QCOMPARE(backend.selected, QString("a"));
        QCOMPARE(backend.connection, BenchmarkBackend::Connected);
        QCOMPARE(backend.protocolPreferences.last(), QString("awg"));
        const auto rows = controller.ranking();
        QCOMPARE(rows[0].toMap().value("countryCode").toString(), QString("c"));
        QCOMPARE(rows[1].toMap().value("countryCode").toString(), QString("a"));
        QCOMPARE(rows[2].toMap().value("state").toString(), QString("timeout"));
        QCOMPARE(references.size(), 5);
        QCOMPARE(trace[0].toMap().value("count").toInt(), 5);
        QCOMPARE(trace[3].toMap().value("count").toInt(), 30);
        QCOMPARE(trace[3].toMap().value("interval").toInt(), 1000);
        QCOMPARE(rows[0].toMap().value("sent").toInt(), 30);
        QCOMPARE(rows[0].toMap().value("measurementStage").toString(), QString("detailed"));
        for (const auto &reference : references) QCOMPARE(reference, QHostAddress("8.8.8.8"));
    }
    void originallyDisconnectedStaysDisconnected() {
        FakeBackend backend; backend.connection = BenchmarkBackend::Disconnected;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
        QCOMPARE(backend.connection, BenchmarkBackend::Disconnected);
        QCOMPARE(backend.connects, 5);
        QCOMPARE(backend.selected, QString("a"));
    }
    void cancellationSerializesPendingApiAndRestoration() {
        FakeBackend backend; backend.delayLoad = true;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(bool(backend.pending), 1000);
        controller.stop();
        QCOMPARE(backend.loads.size(), 1);
        QVERIFY(controller.busy());
        backend.delayLoad = false;
        backend.completePending();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QCOMPARE(backend.loads, QStringList({"a", "a"}));
        QCOMPARE(backend.connection, BenchmarkBackend::Connected);
        QVERIFY(references.isEmpty());
    }
    void currentLocationDoesNotSwitchOrIssueConfigs() {
        FakeBackend backend;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.checkCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QVERIFY(backend.loads.isEmpty()); QCOMPARE(backend.disconnects, 0); QCOMPARE(backend.connects, 0);
        QCOMPARE(controller.ranking()[0].toMap().value("latencyMs").toInt(), 30);
        QCOMPARE(trace[0].toMap().value("count").toInt(), 30);
    }
    void failedCountryDoesNotStopTheEntireScan() {
        FakeBackend backend; backend.failConnection = "b";
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QCOMPARE(controller.ranking().last().toMap().value("state").toString(), QString("connectionError"));
        QCOMPARE(backend.selected, QString("a"));
    }
    void stopBeforeDnsDoesNotSwitchAnything() {
        FakeBackend backend;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll(); controller.stop();
        QTest::qWait(20);
        QVERIFY(!controller.busy()); QVERIFY(backend.loads.isEmpty()); QCOMPARE(backend.disconnects, 0);
    }
    void invalidSubscriptionCannotStart() {
        FakeBackend backend;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("other"); controller.startAll();
        QVERIFY(!controller.busy()); QVERIFY(backend.loads.isEmpty());
    }
    void staleApiResponseCannotCompleteANewScan() {
        FakeBackend backend; backend.delayLoad = true;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(bool(backend.pending), 1000);
        const auto stale = backend.pending; backend.pending = {};
        controller.refresh("other"); controller.refresh("premium"); controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(bool(backend.pending), 1000);
        stale();
        QTest::qWait(20);
        QVERIFY(controller.busy());
        QCOMPARE(backend.connects, 0);
        QCOMPARE(backend.loads.size(), 2);
        backend.delayLoad = false;
        backend.completePending();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QCOMPARE(backend.loads, QStringList({"a", "a", "b", "c", "c", "a", "a"}));
        QCOMPARE(references.size(), 5);
    }
    void staleDnsResponseCannotFailANewScan() {
        FakeBackend backend;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll(); controller.stop();
        emit backend.referenceResolved(1, QHostAddress());
        controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QCOMPARE(references.size(), 5);
        QCOMPARE(backend.selected, QString("a"));
    }
    void statisticsUseMedianNearestRankAndChronologicalVariation() {
        const auto result = PingStatistics::summarize({10, 20, -1, 40, 100});
        QCOMPARE(result.value("replies").toInt(), 4);
        QCOMPARE(result.value("loss").toDouble(), 20.0);
        QCOMPARE(result.value("latencyMs").toDouble(), 42.5);
        QCOMPARE(result.value("medianMs").toDouble(), 30.0);
        QCOMPARE(result.value("p95Ms").toInt(), 100);
        QCOMPARE(result.value("jitterPairs").toInt(), 2);
        QCOMPARE(result.value("jitterMs").toDouble(), 35.0);
        QVector<int> thirty(30, 10); thirty[28] = 50; thirty[29] = 900;
        QCOMPARE(PingStatistics::summarize(thirty).value("p95Ms").toInt(), 50);
    }
    void missingRepliesNeverBecomeZeroLatencyOrZeroVariation() {
        const auto none = PingStatistics::summarize({-1, -1, -1});
        QCOMPARE(none.value("state").toString(), QString("timeout"));
        QCOMPARE(none.value("loss").toDouble(), 100.0);
        QCOMPARE(none.value("medianMs").toInt(), -1);
        QCOMPARE(none.value("jitterMs").toInt(), -1);
        const auto isolated = PingStatistics::summarize({10, -1, 20});
        QCOMPARE(isolated.value("jitterMs").toInt(), -1);
        QVERIFY(qAbs(isolated.value("loss").toDouble() - 100.0 / 3) < 0.001);
    }
    void stableLocationBeatsLowerMinimumWithSpikes() {
        FakeBackend backend;
        patterns["a"] = {50}; patterns["c"] = {20, 200};
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QCOMPARE(controller.ranking()[0].toMap().value("countryCode").toString(), QString("a"));
        QCOMPARE(controller.ranking()[1].toMap().value("jitterMs").toDouble(), 180.0);
    }
    void detailedPassIsLimitedToFiveCandidates() {
        FakeBackend backend; backend.codes = {"a", "b", "c", "d", "e", "f", "g"};
        for (int i = 0; i < backend.codes.size(); ++i) patterns[backend.codes[i]] = {70 - 10 * i};
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
        QCOMPARE(trace.size(), 12);
        QStringList detailed;
        for (const auto &entry : trace) if (entry.toMap().value("count").toInt() == 30) detailed.append(entry.toMap().value("country").toString());
        QCOMPARE(detailed, QStringList({"g", "f", "e", "d", "c"}));
        QCOMPARE(controller.ranking()[5].toMap().value("measurementStage").toString(), QString("quick"));
    }
    void stopDuringDetailedMeasurementRestoresOriginalConnection() {
        FakeBackend backend; holdDetailed = true;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(trace.size() == 4, 1000);
        controller.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QCOMPARE(backend.selected, QString("a"));
        QCOMPARE(backend.connection, BenchmarkBackend::Connected);
        QCOMPARE(references.size(), 4);
    }
    void observedLossRanksBelowLosslessLocation() {
        FakeBackend backend;
        patterns["a"] = {70}; patterns["c"] = {10, 10, -1, 10, 10, 10};
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QCOMPARE(controller.ranking()[0].toMap().value("countryCode").toString(), QString("a"));
        QVERIFY(controller.ranking()[1].toMap().value("loss").toDouble() > 0);
    }
    void protocolChangeDuringMeasurementStopsWithoutRecordingMixedSamples() {
        FakeBackend backend; holdDetailed = true;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.checkCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(trace.size() == 1, 1000);
        backend.proto = "xray"; emit backend.changed();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QVERIFY(controller.ranking()[0].toMap().value("state").toString().isEmpty());
        QCOMPARE(backend.disconnects, 0);
    }
    void noGoogleRepliesSkipDetailedPassAndRestore() {
        FakeBackend backend;
        for (const auto &code : backend.codes) patterns[code] = {-1};
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
        QCOMPARE(trace.size(), 3);
        QCOMPARE(backend.selected, QString("a"));
        QCOMPARE(backend.connection, BenchmarkBackend::Connected);
        for (const auto &item : controller.ranking()) QCOMPARE(item.toMap().value("state").toString(), QString("timeout"));
    }
    void favoritesAndResultsSurviveRestartWithoutRawNetworkData() {
        FakeBackend backend;
        {
            PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
            controller.refresh("premium"); controller.toggleFavorite("c"); controller.startAll();
            QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 2000);
            QCOMPARE(controller.recommendation().value("countryCode").toString(), QString("c"));
        }
        const auto encoded = QJsonDocument::fromVariant(backend.saved.value("premium")).toJson();
        QVERIFY(!encoded.contains("observationsMs"));
        QVERIFY(!encoded.contains("sourceAddress"));
        PremiumBenchmarkController reopened(&backend, nullptr, factory(backend), 0);
        reopened.refresh("premium");
        QCOMPARE(reopened.favoriteCount(), 1);
        QCOMPARE(reopened.ranking()[0].toMap().value("medianMs").toInt(), 10);
        QVERIFY(reopened.ranking()[0].toMap().value("saved").toBool());
        QVERIFY(!reopened.recommendation().isEmpty());
        reopened.refresh("other");
        QCOMPARE(reopened.favoriteCount(), 0);
        QVERIFY(reopened.recommendation().isEmpty());
        reopened.refresh("premium"); QCOMPARE(reopened.favoriteCount(), 1);
    }
    void favoriteScanOnlyTestsSelectedCountriesInDetail() {
        FakeBackend backend;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.toggleFavorite("c"); controller.startFavorites();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QCOMPARE(trace.size(), 1);
        QCOMPARE(trace[0].toMap().value("country").toString(), QString("c"));
        QCOMPARE(trace[0].toMap().value("count").toInt(), 30);
        QCOMPARE(backend.loads, QStringList({"c", "a"}));
        QCOMPARE(backend.connection, BenchmarkBackend::Connected);
        QCOMPARE(controller.overallProgress(), 1.0);
    }
    void oneCountryCheckPreservesOtherHistoryAndRestoresConnection() {
        FakeBackend backend;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        trace.clear(); backend.loads.clear();
        controller.checkCountry("a");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QCOMPARE(trace.size(), 1);
        QCOMPARE(controller.recommendation().value("countryCode").toString(), QString("a"));
        auto previous = controller.ranking()[1].toMap();
        QCOMPARE(previous.value("countryCode").toString(), QString("c"));
        QVERIFY(previous.value("historical").toBool());
        QCOMPARE(previous.value("medianMs").toInt(), 10);
        QCOMPARE(backend.selected, QString("a"));
    }
    void staleSavedResultsAreLabeledAndNeverRecommended() {
        FakeBackend backend;
        {
            PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
            controller.refresh("premium"); controller.checkCurrent();
            QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        }
        auto stored = backend.saved["premium"];
        auto rows = stored.value("rows").toList();
        auto row = rows[0].toMap();
        row["checkedAtUtc"] = QDateTime::currentDateTimeUtc().addDays(-2).toString(Qt::ISODateWithMs);
        rows[0] = row; stored["rows"] = rows; backend.saved["premium"] = stored;
        PremiumBenchmarkController reopened(&backend, nullptr, factory(backend), 0);
        reopened.refresh("premium");
        QVERIFY(reopened.recommendation().isEmpty());
        QVERIFY(reopened.ranking()[0].toMap().value("stale").toBool());
    }
    void invalidSelectionsAndEmptyFavoritesDoNotChangeVpn() {
        FakeBackend backend;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startFavorites(); controller.checkCountry("unknown"); controller.toggleFavorite("unknown");
        QVERIFY(!controller.busy()); QVERIFY(backend.loads.isEmpty());
        QCOMPARE(controller.favoriteCount(), 0);
    }
    void progressAndFavoritesAreProtectedDuringActiveCheck() {
        FakeBackend backend; holdDetailed = true;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.toggleFavorite("c"); controller.startFavorites();
        QTRY_VERIFY_WITH_TIMEOUT(trace.size() == 1, 1000);
        controller.toggleFavorite("a");
        QCOMPARE(controller.favoriteCount(), 1);
        QVERIFY(!controller.progressText().isEmpty());
        QVERIFY(controller.recommendation().isEmpty());
        QVERIFY(controller.overallProgress() >= 0 && controller.overallProgress() < 1);
        controller.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QCOMPARE(backend.selected, QString("a"));
    }
};
QTEST_GUILESS_MAIN(TestPremiumBenchmark)
#include "testPremiumBenchmark.moc"
