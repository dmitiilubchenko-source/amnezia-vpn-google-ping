#ifndef GOOGLEPINGPROBE_H
#define GOOGLEPINGPROBE_H
#include <QHostAddress>
#include <QObject>
#include <QVariantMap>
#include <atomic>
#include <memory>

class BenchmarkProbe : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void start(const QHostAddress &destination) = 0;
signals:
    void finished(const QVariantMap &result);
};

class GooglePingProbe final : public BenchmarkProbe {
    Q_OBJECT
public:
    explicit GooglePingProbe(QObject *parent = nullptr);
    ~GooglePingProbe() override;
    void start(const QHostAddress &destination) override;
private:
    std::shared_ptr<std::atomic_bool> m_cancelled;
};
#endif
