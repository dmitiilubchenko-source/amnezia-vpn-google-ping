#include "premiumLatencyController.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include "core/repositories/secureServersRepository.h"
#include "secureQSettings.h"
#include "ui/controllers/connectionUiController.h"
#include "serverLatencyProbe.h"
#include "tunnelLatencyProbe.h"
#include "core/repositories/secureAppSettingsRepository.h"

namespace {
constexpr char cacheKey[] = "Conf/premiumLatencyTargets";
constexpr qint64 cacheLifetime = 24 * 60 * 60;
}

PremiumLatencyController::PremiumLatencyController(SecureServersRepository *servers, SecureQSettings *settings,
                                                   ConnectionUiController *connection, QObject *parent)
    : QObject(parent), m_servers(servers), m_settings(settings), m_connection(connection)
{
    m_cache = QJsonDocument::fromJson(m_settings->value(cacheKey).toByteArray()).object();
    // Backups/restores and removed subscriptions must not leave orphan targets.
    const auto serverIds = m_servers->orderedServerIds();
    for (const auto &id : m_cache.keys()) {
        if (!serverIds.contains(id)) {
            m_cache.remove(id);
        }
    }
    for (const auto &id : serverIds) {
        rememberEndpoint(id);
    }
    saveCache();
    connect(m_servers, &SecureServersRepository::serverAdded, this, &PremiumLatencyController::rememberEndpoint);
    connect(m_servers, &SecureServersRepository::serverEdited, this, &PremiumLatencyController::rememberEndpoint);
    connect(m_servers, &SecureServersRepository::serverRemoved, this, [this](const QString &id) {
        m_cache.remove(id);
        saveCache();
        if (m_serverId == id) {
            refresh({});
        }
    });
    connect(m_connection, &ConnectionUiController::connectionStateChanged, this, [this]() {
        cancel();
        m_results.clear();
        updateResults();
        emit availabilityChanged();
    });
}

PremiumLatencyController::~PremiumLatencyController()
{
    cancel();
}

bool PremiumLatencyController::supported() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool PremiumLatencyController::available() const
{
    return supported() && !m_connection->isConnected() && !m_connection->isConnectionInProgress();
}

bool PremiumLatencyController::isPremium() const
{
    const auto config = m_servers->apiV2Config(m_serverId);
    return config && config->isPremium();
}

bool PremiumLatencyController::busy() const
{
    return m_tunnelProbe || !m_probes.isEmpty() || !m_pending.isEmpty();
}

bool PremiumLatencyController::tunnelAvailable() const
{
    return supported() && isPremium() && m_connection->isConnected() && !m_connection->isConnectionInProgress()
        && m_servers->defaultServerId() == m_serverId
        && m_appSettings && !m_appSettings->isAppsSplitTunnelingEnabled();
}

void PremiumLatencyController::setAppSettings(SecureAppSettingsRepository *settings)
{
    m_appSettings = settings;
    connect(settings, &SecureAppSettingsRepository::appsSplitTunnelingEnabledChanged, this, [this](bool enabled) {
        if (enabled) cancel();
        emit availabilityChanged();
    });
    connect(m_servers, &SecureServersRepository::defaultServerChanged, this, [this]() {
        cancel();
        emit availabilityChanged();
    });
    emit availabilityChanged();
}

void PremiumLatencyController::checkConnected(const QString &serverId)
{
    if (busy()) return;
    refresh(serverId);
    if (!tunnelAvailable()) return;
    const auto config = m_servers->apiV2Config(serverId);
    const QString country = config->apiConfig.serverCountryCode;
    const QString protocol = config->serviceProtocol();
    auto *probe = new TunnelLatencyProbe(this);
    m_tunnelProbe = probe;
    m_tunnelResults[country] = QVariantMap{{"state", "checking"}};
    emit resultsChanged();
    emit busyChanged();
    connect(probe, &TunnelLatencyProbe::finished, this, [this, probe, serverId, country, protocol](QVariantMap result) {
        m_tunnelProbe = nullptr;
        probe->deleteLater();
        const auto current = m_servers->apiV2Config(serverId);
        if (m_serverId == serverId && current && current->apiConfig.serverCountryCode == country
            && current->serviceProtocol() == protocol && tunnelAvailable()) {
            result["measuredAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
            m_tunnelResults[country] = result;
        }
        emit resultsChanged();
        emit busyChanged();
    });
    probe->start();
}

QVariantMap PremiumLatencyController::results() const
{
    return m_results;
}

void PremiumLatencyController::saveCache()
{
    m_settings->setValue(cacheKey, QJsonDocument(m_cache).toJson(QJsonDocument::Compact));
}

void PremiumLatencyController::rememberEndpoint(const QString &serverId)
{
    const auto config = m_servers->apiV2Config(serverId);
    if (!config || !config->isPremium() || !config->hasContainers()
        || config->hostName.isEmpty() || config->apiConfig.serverCountryCode.isEmpty()) {
        return;
    }
    auto countries = m_cache.value(serverId).toObject();
    const QString country = config->apiConfig.serverCountryCode;
    const auto previous = countries.value(country).toObject();
    countries[country] = QJsonObject{{"host", config->hostName}, {"protocol", config->serviceProtocol()},
                                    {"seen", QDateTime::currentSecsSinceEpoch()}};
    m_cache[serverId] = countries;
    saveCache();
    if (m_serverId == serverId) {
        // A new endpoint invalidates even a successful measurement of the old one.
        if (previous.value("host").toString() != config->hostName
            || previous.value("protocol").toString() != config->serviceProtocol()) {
            cancel();
            m_results.remove(country);
            m_tunnelResults.remove(country);
        }
        if (m_comparisonProtocol != config->serviceProtocol()) {
            m_tunnelResults.clear();
            m_comparisonProtocol = config->serviceProtocol();
        }
        updateResults();
    }
}

QString PremiumLatencyController::targetHost(const QString &countryCode) const
{
    const auto config = m_servers->apiV2Config(m_serverId);
    if (!config || !config->isPremium()) {
        return {};
    }
    const auto target = m_cache.value(m_serverId).toObject().value(countryCode).toObject();
    const qint64 age = QDateTime::currentSecsSinceEpoch() - target.value("seen").toInteger();
    if (age < 0 || age > cacheLifetime || target.value("protocol").toString() != config->serviceProtocol()) {
        return {};
    }
    return target.value("host").toString();
}

void PremiumLatencyController::refresh(const QString &serverId)
{
    if (m_serverId != serverId) {
        cancel();
        m_results.clear();
        m_tunnelResults.clear();
        m_serverId = serverId;
        const auto config = m_servers->apiV2Config(serverId);
        m_comparisonProtocol = config ? config->serviceProtocol() : QString();
    }
    updateResults();
    emit availabilityChanged();
}

void PremiumLatencyController::updateResults()
{
    const auto config = m_servers->apiV2Config(m_serverId);
    QVariantMap current;
    if (config && config->isPremium()) {
        for (const auto &country : config->apiConfig.availableCountries) {
            const auto code = country.toObject().value(apiDefs::key::serverCountryCode).toString();
            if (code.isEmpty()) {
                continue;
            }
            const bool known = !targetHost(code).isEmpty();
            const auto previous = m_results.value(code).toMap();
            current[code] = known && !previous.isEmpty() ? previous
                : QVariantMap{{"state", known ? "idle" : "unknown"}};
        }
    }
    m_results = current;
    emit resultsChanged();
}

void PremiumLatencyController::checkAll(const QString &serverId)
{
    if (!available() || busy()) {
        return;
    }
    refresh(serverId);
    m_results.clear();
    updateResults();
    for (auto it = m_results.begin(); it != m_results.end(); ++it) {
        if (!targetHost(it.key()).isEmpty()) {
            m_pending.enqueue(it.key());
            it.value() = QVariantMap{{"state", "checking"}};
        }
    }
    emit resultsChanged();
    for (int i = 0; i < 4; ++i) {
        startNext();
    }
    emit busyChanged();
}

void PremiumLatencyController::startNext()
{
    if (m_pending.isEmpty() || !available()) {
        return;
    }
    const auto country = m_pending.dequeue();
    auto *probe = new ServerLatencyProbe(this);
    m_probes.insert(probe);
    connect(probe, &ServerLatencyProbe::finished, this, [this, probe, country](const QVariantMap &measurement) {
        m_probes.remove(probe);
        probe->deleteLater();
        m_results[country] = measurement;
        emit resultsChanged();
        startNext();
        emit busyChanged();
    });
    probe->start(targetHost(country));
}

void PremiumLatencyController::cancel()
{
    auto *tunnelProbe = m_tunnelProbe;
    m_tunnelProbe = nullptr;
    delete tunnelProbe;
    for (auto it = m_tunnelResults.begin(); it != m_tunnelResults.end(); ++it) {
        if (it.value().toMap().value("state") == "checking") {
            it.value() = QVariantMap{{"state", "idle"}};
        }
    }
    m_pending.clear();
    const auto probes = m_probes;
    m_probes.clear();
    for (auto *probe : probes) {
        delete probe;
    }
    for (auto it = m_results.begin(); it != m_results.end(); ++it) {
        if (it.value().toMap().value("state") == "checking") {
            it.value() = QVariantMap{{"state", "idle"}};
        }
    }
    emit resultsChanged();
    emit busyChanged();
}

void PremiumLatencyController::clearCache()
{
    cancel();
    m_cache.remove(m_serverId);
    m_results.clear();
    saveCache();
    updateResults();
}
