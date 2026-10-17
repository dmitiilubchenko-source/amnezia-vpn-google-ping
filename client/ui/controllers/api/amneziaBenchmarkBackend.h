#ifndef AMNEZIABENCHMARKBACKEND_H
#define AMNEZIABENCHMARKBACKEND_H
#include "premiumBenchmarkController.h"
class SecureServersRepository;
class SecureAppSettingsRepository;
class ConnectionController;
class ConnectionUiController;
class SubscriptionController;
class SecureQSettings;

class AmneziaBenchmarkBackend final : public BenchmarkBackend {
    Q_OBJECT
public:
    AmneziaBenchmarkBackend(SecureServersRepository *servers, SecureAppSettingsRepository *settings,
        ConnectionController *connection, ConnectionUiController *ui, SubscriptionController *subscription, SecureQSettings *storage, QObject *parent);
    bool eligible(const QString &serverId) const override;
    State state() const override;
    QVariantList countries(const QString &serverId) const override;
    QString country(const QString &serverId) const override;
    QString protocol(const QString &serverId) const override;
    void resolveReference(quint64 requestId, std::shared_ptr<std::atomic_bool> cancelled) override;
    void load(const QString &serverId, const QString &country, const QString &preferredProtocol,
              quint64 requestId, std::shared_ptr<std::atomic_bool> cancelled) override;
    void connectVpn() override;
    void disconnectVpn() override;
    QVariantMap preferences(const QString &id) const override;
    void savePreferences(const QString &id, const QVariantMap &preferences) override;
private:
    SecureServersRepository *m_servers;
    SecureAppSettingsRepository *m_settings;
    ConnectionController *m_connection;
    ConnectionUiController *m_ui;
    SubscriptionController *m_subscription;
    SecureQSettings *m_storage;
    bool m_failed = false;
};
#endif
