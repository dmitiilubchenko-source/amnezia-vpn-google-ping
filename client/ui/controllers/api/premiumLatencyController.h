#ifndef PREMIUMLATENCYCONTROLLER_H
#define PREMIUMLATENCYCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QVariantMap>

class SecureQSettings;
class SecureServersRepository;
class ConnectionUiController;
class ServerLatencyProbe;
class TunnelLatencyProbe;
class SecureAppSettingsRepository;

class PremiumLatencyController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool isPremium READ isPremium NOTIFY resultsChanged)
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap results READ results NOTIFY resultsChanged)
    Q_PROPERTY(QString status READ status NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap tunnelResults READ tunnelResults NOTIFY resultsChanged)
    Q_PROPERTY(bool tunnelAvailable READ tunnelAvailable NOTIFY availabilityChanged)
public:
    PremiumLatencyController(SecureServersRepository *servers, SecureQSettings *settings,
                             ConnectionUiController *connection, QObject *parent = nullptr);
    ~PremiumLatencyController() override;
    bool supported() const;
    bool isPremium() const;
    bool available() const;
    bool busy() const;
    QVariantMap results() const;
    QString status() const;
    QVariantMap tunnelResults() const { return m_tunnelResults; }
    bool tunnelAvailable() const;
    void setAppSettings(SecureAppSettingsRepository *settings);

public slots:
    void refresh(const QString &serverId);
    void checkAll(const QString &serverId);
    void cancel();
    void clearCache();
    void checkConnected(const QString &serverId);

signals:
    void availabilityChanged();
    void busyChanged();
    void resultsChanged();

private:
    void rememberEndpoint(const QString &serverId);
    void saveCache();
    void updateResults();
    void startNext();
    QString targetHost(const QString &countryCode) const;
    QString m_serverId;
    SecureServersRepository *m_servers;
    SecureQSettings *m_settings;
    ConnectionUiController *m_connection;
    QJsonObject m_cache;
    QVariantMap m_results;
    QQueue<QString> m_pending;
    QSet<ServerLatencyProbe *> m_probes;
    TunnelLatencyProbe *m_tunnelProbe = nullptr;
    SecureAppSettingsRepository *m_appSettings = nullptr;
    QVariantMap m_tunnelResults;
    QString m_comparisonProtocol;
    bool m_hasRun = false;
};

#endif
