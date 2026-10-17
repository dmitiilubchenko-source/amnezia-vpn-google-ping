#ifndef GOOGLEPINGPROBE_H
#define GOOGLEPINGPROBE_H
#include <QHostAddress>
#include <QObject>
#include <QVariantMap>
#include <atomic>
#include <memory>

struct PingSettings {
    int count = 5;
    int intervalMs = 200;
    int timeoutMs = 1000;
    int warmupCount = 1;
};

class BenchmarkProbe : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void start(const QHostAddress &destination, const PingSettings &settings = {}) = 0;
signals:
    void finished(const QVariantMap &result);
    void progress(int sent, int total);
};

class GooglePingProbe final : public BenchmarkProbe {
    Q_OBJECT
public:
    explicit GooglePingProbe(QObject *parent = nullptr);
    ~GooglePingProbe() override;
    void start(const QHostAddress &destination, const PingSettings &settings = {}) override;
private:
    std::shared_ptr<std::atomic_bool> m_cancelled;
};
#endif
