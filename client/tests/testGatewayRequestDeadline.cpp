#include <QTest>
#include "core/controllers/gatewayRequestDeadline.h"

class HangingReply : public QNetworkReply {
public:
    int aborts = 0;
    void abort() override {
        ++aborts;
        setError(QNetworkReply::OperationCanceledError, "cancelled");
        setFinished(true);
        emit finished();
    }
protected:
    qint64 readData(char *, qint64) override { return -1; }
};

class TestGatewayRequestDeadline : public QObject {
    Q_OBJECT
private slots:
    void expiryAbortsTransportBeforeCompletion() {
        GatewayRequestDeadline deadline;
        auto *reply = new HangingReply;
        QPointer<HangingReply> guard(reply);
        deadline.track(reply);
        int completions = 0;
        deadline.start(20, [&]() {
            QVERIFY(deadline.expired());
            QVERIFY(guard);
            QCOMPARE(reply->aborts, 1);
            ++completions;
        });
        QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 500);
        QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 500);
        QTest::qWait(30); QCOMPARE(completions, 1);
    }
    void completedRequestDoesNotExpireOrAbort() {
        GatewayRequestDeadline deadline;
        HangingReply reply;
        deadline.track(&reply);
        bool expired = false;
        deadline.start(20, [&]() { expired = true; });
        deadline.complete();
        QTest::qWait(50);
        QVERIFY(!expired); QVERIFY(!deadline.expired()); QCOMPARE(reply.aborts, 0);
    }
    void expiryAbortsFallbackRepliesAndRejectsLateOnes() {
        GatewayRequestDeadline deadline;
        auto *initial = new HangingReply;
        deadline.track(initial);
        delete initial; // The initial request has already finished.
        auto *fallback = new HangingReply;
        QPointer<HangingReply> fallbackGuard(fallback);
        deadline.track(fallback);
        bool completed = false;
        deadline.start(20, [&]() { QCOMPARE(fallback->aborts, 1); completed = true; });
        QTRY_VERIFY_WITH_TIMEOUT(completed, 500);
        QTRY_VERIFY_WITH_TIMEOUT(fallbackGuard.isNull(), 500);
        auto *late = new HangingReply;
        QPointer<HangingReply> lateGuard(late);
        deadline.track(late);
        QCOMPARE(late->aborts, 1);
        QTRY_VERIFY_WITH_TIMEOUT(lateGuard.isNull(), 500);
    }
};
QTEST_GUILESS_MAIN(TestGatewayRequestDeadline)
#include "testGatewayRequestDeadline.moc"
