#include "amneziaBenchmarkBackend.h"
#include <QFutureWatcher>
#include <QHostInfo>
#include <QJsonDocument>
#include "secureQSettings.h"
#include "core/repositories/secureServersRepository.h"
#include "core/repositories/secureAppSettingsRepository.h"
#include "core/controllers/connectionController.h"
#include "core/controllers/api/subscriptionController.h"
#include "ui/controllers/connectionUiController.h"
#include "core/utils/api/apiUtils.h"

AmneziaBenchmarkBackend::AmneziaBenchmarkBackend(SecureServersRepository *servers, SecureAppSettingsRepository *settings,
    ConnectionController *connection, ConnectionUiController *ui, SubscriptionController *subscription, SecureQSettings *storage, QObject *parent)
    : BenchmarkBackend(parent), m_servers(servers), m_settings(settings), m_connection(connection), m_ui(ui), m_subscription(subscription), m_storage(storage)
{
    connect(ui, &ConnectionUiController::connectionStateChanged, this, &BenchmarkBackend::changed);
    connect(servers, &SecureServersRepository::serverEdited, this, &BenchmarkBackend::changed);
    connect(servers, &SecureServersRepository::serverRemoved, this, &BenchmarkBackend::changed);
    connect(servers, &SecureServersRepository::defaultServerChanged, this, &BenchmarkBackend::changed);
    connect(settings, &SecureAppSettingsRepository::appsSplitTunnelingEnabledChanged, this, &BenchmarkBackend::changed);
    connect(servers, &SecureServersRepository::serverRemoved, this, [this](const QString &id) {
        auto all = QJsonDocument::fromJson(m_storage->value("Conf/premiumBenchmarkV5").toByteArray()).object();
        all.remove(id);
        m_storage->setValue("Conf/premiumBenchmarkV5", QJsonDocument(all).toJson(QJsonDocument::Compact));
    });
    auto all = QJsonDocument::fromJson(m_storage->value("Conf/premiumBenchmarkV5").toByteArray()).object();
    const auto ids = servers->orderedServerIds();
    bool pruned = false;
    for (const auto &id : all.keys()) if (!ids.contains(id)) { all.remove(id); pruned = true; }
    if (pruned) m_storage->setValue("Conf/premiumBenchmarkV5", QJsonDocument(all).toJson(QJsonDocument::Compact));
}

QVariantMap AmneziaBenchmarkBackend::preferences(const QString &id) const
{
    return QJsonDocument::fromJson(m_storage->value("Conf/premiumBenchmarkV5").toByteArray()).object().value(id).toObject().toVariantMap();
}

void AmneziaBenchmarkBackend::savePreferences(const QString &id, const QVariantMap &preferences)
{
    if (!m_servers->orderedServerIds().contains(id)) return;
    auto all = QJsonDocument::fromJson(m_storage->value("Conf/premiumBenchmarkV5").toByteArray()).object();
    all[id] = QJsonObject::fromVariantMap(preferences);
    m_storage->setValue("Conf/premiumBenchmarkV5", QJsonDocument(all).toJson(QJsonDocument::Compact));
}

bool AmneziaBenchmarkBackend::eligible(const QString &id) const
{
#ifdef Q_OS_WIN
    const auto config = m_servers->apiV2Config(id);
    return config && config->isPremium() && !config->apiConfig.isSubscriptionExpired()
        && m_servers->defaultServerId() == id && !m_settings->isAppsSplitTunnelingEnabled();
#else
    Q_UNUSED(id)
    return false;
#endif
}

BenchmarkBackend::State AmneziaBenchmarkBackend::state() const
{
    if (m_failed) return Error;
    if (m_ui->isConnected()) return Connected;
    if (m_ui->isConnectionInProgress()) return Connecting;
    return Disconnected;
}

QString AmneziaBenchmarkBackend::country(const QString &id) const
{
    const auto config = m_servers->apiV2Config(id);
    return config ? config->apiConfig.serverCountryCode : QString();
}

QString AmneziaBenchmarkBackend::protocol(const QString &id) const
{
    const auto config = m_servers->apiV2Config(id);
    return config ? config->serviceProtocol() : QString();
}

QVariantList AmneziaBenchmarkBackend::countries(const QString &id) const
{
    QVariantList rows;
    const auto config = m_servers->apiV2Config(id);
    if (!config || !config->isPremium()) return rows;
    for (const auto &item : config->apiConfig.availableCountries) {
        const auto object = item.toObject();
        const auto code = object.value(apiDefs::key::serverCountryCode).toString();
        const auto name = object.value(apiDefs::key::serverCountryName).toString();
        if (code.isEmpty()) continue;
        rows.append(QVariantMap{{"countryCode", code}, {"countryName", name.isEmpty() ? code : name},
            {"countryImageCode", apiUtils::getCountryFlagCode(object.value(apiDefs::key::serverCountryCodeL10n).toString(), code)}});
    }
    return rows;
}

void AmneziaBenchmarkBackend::resolveReference(quint64 requestId, std::shared_ptr<std::atomic_bool> cancelled)
{
    auto *context = new QObject(this);
    auto delivered = std::make_shared<std::atomic_bool>(false);
    const int lookup = QHostInfo::lookupHost("google.com", context, [this, context, delivered, cancelled, requestId](const QHostInfo &info) {
        if (delivered->exchange(true)) return;
        context->deleteLater();
        if (cancelled->load()) return;
        for (const auto &address : info.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) { emit referenceResolved(requestId, address); return; }
        }
        emit referenceResolved(requestId, {});
    });
    QTimer::singleShot(4000, context, [this, context, delivered, cancelled, lookup, requestId]() {
        if (delivered->exchange(true)) return;
        QHostInfo::abortHostLookup(lookup);
        context->deleteLater();
        if (!cancelled->load()) emit referenceResolved(requestId, {});
    });
}

void AmneziaBenchmarkBackend::load(const QString &id, const QString &code, const QString &preferred,
                                  quint64 requestId, std::shared_ptr<std::atomic_bool> cancelled)
{
    auto *watcher = new QFutureWatcher<amnezia::ErrorCode>(this);
    connect(watcher, &QFutureWatcher<amnezia::ErrorCode>::finished, this, [this, watcher, cancelled, requestId]() {
        const auto error = watcher->result();
        watcher->deleteLater();
        emit loaded(requestId, !cancelled->load() && error == amnezia::ErrorCode::NoError, error == amnezia::ErrorCode::ApiCaptchaRequiredError);
    });
    watcher->setFuture(m_subscription->updateBenchmarkCountry(id, code, this, cancelled, preferred));
}

void AmneziaBenchmarkBackend::connectVpn()
{
    m_failed = false;
    const auto error = m_connection->openConnection(m_servers->defaultServerId());
    if (error != amnezia::ErrorCode::NoError) { m_failed = true; emit changed(); }
}
void AmneziaBenchmarkBackend::disconnectVpn() { m_failed = false; m_connection->closeConnection(); }
