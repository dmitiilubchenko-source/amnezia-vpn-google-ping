#ifndef PREMIUMBENCHMARKCONTROLLER_H
#define PREMIUMBENCHMARKCONTROLLER_H
#include "googlePingProbe.h"
#include <QTimer>
#include <QVariantList>
#include <functional>

class BenchmarkBackend : public QObject {
    Q_OBJECT
public:
    enum State { Disconnected, Connecting, Connected, Error };
    using QObject::QObject;
    virtual bool eligible(const QString &serverId) const = 0;
    virtual State state() const = 0;
    virtual QVariantList countries(const QString &serverId) const = 0;
    virtual QString country(const QString &serverId) const = 0;
    virtual QString protocol(const QString &serverId) const = 0;
    virtual void resolveReference(quint64 requestId, std::shared_ptr<std::atomic_bool> cancelled) = 0;
    virtual void load(const QString &serverId, const QString &country, const QString &preferredProtocol,
                      quint64 requestId, std::shared_ptr<std::atomic_bool> cancelled) = 0;
    virtual void connectVpn() = 0;
    virtual void disconnectVpn() = 0;
signals:
    void changed();
    void referenceResolved(quint64 requestId, const QHostAddress &address);
    void loaded(quint64 requestId, bool success, bool captcha);
};

class PremiumBenchmarkController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canStart READ canStart NOTIFY changed)
    Q_PROPERTY(bool canMeasure READ canMeasure NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString currentCountry READ currentCountry NOTIFY changed)
    Q_PROPERTY(QVariantList ranking READ ranking NOTIFY changed)
public:
    using ProbeFactory = std::function<BenchmarkProbe *(QObject *)>;
    PremiumBenchmarkController(BenchmarkBackend *backend, QObject *parent = nullptr, ProbeFactory factory = {}, int settleMsecs = 2000);
    ~PremiumBenchmarkController() override;
    bool busy() const { return m_phase != Idle; }
    bool canStart() const { return !busy() && m_backend->eligible(m_serverId) && m_backend->state() != BenchmarkBackend::Connecting; }
    bool canMeasure() const { return canStart() && m_backend->state() == BenchmarkBackend::Connected; }
    QString status() const { return m_status; }
    QString currentCountry() const { return m_backend->country(m_serverId); }
    QVariantList ranking() const;
public slots:
    void refresh(const QString &serverId);
    void startAll();
    void checkCurrent();
    void stop();
signals:
    void changed();
private:
    enum Phase { Idle, Resolving, Disconnecting, Loading, Connecting, Settling, Measuring };
    void begin(bool single);
    void prepareTarget();
    void loadTarget();
    void onConnectionChanged();
    void onLoaded(quint64 requestId, bool success, bool captcha);
    void settle();
    void measure();
    void record(const QVariantMap &result);
    void advance();
    bool beginDetailedPass();
    void restore();
    void finish(const QString &message);
    void onTimeout();
    QString target() const;
    QString targetName() const;
    PingSettings probeSettings() const;
    QString completionMessage() const;
    void setPhase(Phase phase, int timeoutMsecs, const QString &message);
    BenchmarkBackend *m_backend;
    ProbeFactory m_factory;
    int m_settleMsecs;
    QTimer m_timeout;
    Phase m_phase = Idle;
    QString m_serverId, m_originalCountry, m_originalProtocol, m_measureProtocol, m_status;
    QVariantList m_rows;
    QStringList m_order;
    int m_cursor = 0;
    quint64 m_requestId = 0;
    bool m_single = false, m_detailed = false, m_originalConnected = false, m_restoring = false, m_stopping = false;
    QHostAddress m_reference;
    BenchmarkProbe *m_probe = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};
#endif
