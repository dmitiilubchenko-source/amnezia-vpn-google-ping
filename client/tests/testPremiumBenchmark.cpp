#include <QTest>
#include <QTimer>
#include "ui/controllers/api/premiumBenchmarkController.h"

class FakeBackend final : public BenchmarkBackend {
public:
    State connection = Connected;
    QString selected = "a", proto = "awg", failConnection;
    bool allowed = true, delayLoad = false;
    QStringList loads, preferences;
    int connects = 0, disconnects = 0;
    std::function<void()> pending;
    bool eligible(const QString &id) const override { return allowed && id == "premium"; }
    State state() const override { return connection; }
    QVariantList countries(const QString &) const override {
        return {QVariantMap{{"countryCode", "a"}, {"countryName", "A"}},
                QVariantMap{{"countryCode", "b"}, {"countryName", "B"}},
                QVariantMap{{"countryCode", "c"}, {"countryName", "C"}}};
    }
    QString country(const QString &) const override { return selected; }
    QString protocol(const QString &) const override { return proto; }
    void resolveReference(quint64 requestId, std::shared_ptr<std::atomic_bool> cancelled) override {
        QTimer::singleShot(0, this, [this, cancelled, requestId]() {
            if (!cancelled->load()) emit referenceResolved(requestId, QHostAddress("8.8.8.8"));
        });
    }
    void load(const QString &, const QString &code, const QString &preferred, quint64 requestId, std::shared_ptr<std::atomic_bool> cancelled) override {
        loads.append(code); preferences.append(preferred);
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
    FakeProbe(QVariantMap result, QList<QHostAddress> *references, QObject *parent)
        : BenchmarkProbe(parent), result(result), references(references) {}
    QVariantMap result;
    QList<QHostAddress> *references;
    void start(const QHostAddress &address) override {
        references->append(address);
        QTimer::singleShot(0, this, [this]() { emit finished(result); });
    }
};

class TestPremiumBenchmark : public QObject {
    Q_OBJECT
private:
    QList<QHostAddress> references;
    PremiumBenchmarkController::ProbeFactory factory(FakeBackend &backend) {
        return [this, &backend](QObject *parent) {
            const int ping = backend.selected == "c" ? 10 : 30;
            return new FakeProbe(backend.selected == "b" ? QVariantMap{{"state", "timeout"}, {"latencyMs", -1}}
                : QVariantMap{{"state", "ready"}, {"latencyMs", ping}, {"loss", 0}, {"replies", 5}}, &references, parent);
        };
    }
private slots:
    void init() { references.clear(); }
    void allLocationsSortAndRestoreConnected() {
        FakeBackend backend;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium");
        controller.startAll();
        QVERIFY(controller.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
        QCOMPARE(backend.loads, QStringList({"a", "b", "c", "a"}));
        QCOMPARE(backend.selected, QString("a"));
        QCOMPARE(backend.connection, BenchmarkBackend::Connected);
        QCOMPARE(backend.preferences.last(), QString("awg"));
        const auto rows = controller.ranking();
        QCOMPARE(rows[0].toMap().value("countryCode").toString(), QString("c"));
        QCOMPARE(rows[1].toMap().value("countryCode").toString(), QString("a"));
        QCOMPARE(rows[2].toMap().value("state").toString(), QString("timeout"));
        QCOMPARE(references.size(), 3);
        for (const auto &reference : references) QCOMPARE(reference, QHostAddress("8.8.8.8"));
    }
    void originallyDisconnectedStaysDisconnected() {
        FakeBackend backend; backend.connection = BenchmarkBackend::Disconnected;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
        QCOMPARE(backend.connection, BenchmarkBackend::Disconnected);
        QCOMPARE(backend.connects, 3);
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
        QCOMPARE(backend.loads, QStringList({"a", "a", "b", "c", "a"}));
        QCOMPARE(references.size(), 3);
    }
    void staleDnsResponseCannotFailANewScan() {
        FakeBackend backend;
        PremiumBenchmarkController controller(&backend, nullptr, factory(backend), 0);
        controller.refresh("premium"); controller.startAll(); controller.stop();
        emit backend.referenceResolved(1, QHostAddress());
        controller.startAll();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 1000);
        QCOMPARE(references.size(), 3);
        QCOMPARE(backend.selected, QString("a"));
    }
};
QTEST_GUILESS_MAIN(TestPremiumBenchmark)
#include "testPremiumBenchmark.moc"
