#ifndef SERVERLATENCYPROBE_H
#define SERVERLATENCYPROBE_H

#include <QHostAddress>
#include <QObject>
#include <QVariantMap>
#include <atomic>
#include <memory>

// One bounded, asynchronous ICMP measurement. Never requests VPN configurations.
class ServerLatencyProbe : public QObject
{
    Q_OBJECT
public:
    explicit ServerLatencyProbe(QObject *parent = nullptr);
    ~ServerLatencyProbe() override;
    void start(const QString &host);

signals:
    void finished(const QVariantMap &result);

private:
    void measure(const QHostAddress &address);
    void complete(const QVariantMap &result);
    int m_lookupId = -1;
    bool m_finished = false;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

#endif
