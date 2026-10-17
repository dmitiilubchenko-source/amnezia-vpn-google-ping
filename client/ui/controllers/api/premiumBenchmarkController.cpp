#include "premiumBenchmarkController.h"
#include <algorithm>

PremiumBenchmarkController::PremiumBenchmarkController(BenchmarkBackend *backend, QObject *parent, ProbeFactory factory, int settleMsecs)
    : QObject(parent), m_backend(backend), m_factory(factory), m_settleMsecs(settleMsecs)
{
    if (!m_factory) m_factory = [](QObject *parent) { return new GooglePingProbe(parent); };
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &PremiumBenchmarkController::onTimeout);
    connect(backend, &BenchmarkBackend::changed, this, &PremiumBenchmarkController::onConnectionChanged, Qt::QueuedConnection);
    connect(backend, &BenchmarkBackend::loaded, this, &PremiumBenchmarkController::onLoaded, Qt::QueuedConnection);
    connect(backend, &BenchmarkBackend::referenceResolved, this, [this](quint64 requestId, const QHostAddress &address) {
        if (m_phase != Resolving || requestId != m_requestId) return;
        if (address.protocol() != QAbstractSocket::IPv4Protocol) { finish(tr("Could not resolve google.com")); return; }
        m_reference = address;
        if (m_single) settle(); else prepareTarget();
    }, Qt::QueuedConnection);
}

QVariantList PremiumBenchmarkController::ranking() const
{
    auto rows = m_rows;
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        const auto left = a.toMap(), right = b.toMap();
        const bool lready = left.value("state") == "ready", rready = right.value("state") == "ready";
        if (lready != rready) return lready;
        if (!lready) return false;
        if (left.value("latencyMs") != right.value("latencyMs")) return left.value("latencyMs").toInt() < right.value("latencyMs").toInt();
        return left.value("loss").toInt() < right.value("loss").toInt();
    });
    return rows;
}

PremiumBenchmarkController::~PremiumBenchmarkController()
{
    if (m_cancelled) m_cancelled->store(true);
    delete m_probe;
}

void PremiumBenchmarkController::refresh(const QString &serverId)
{
    if (serverId != m_serverId) {
        if (busy()) finish(tr("Test stopped: selected subscription changed"));
        m_serverId = serverId;
        m_rows = m_backend->countries(serverId);
        m_reference = {};
        m_status.clear();
    } else if (!busy()) {
        const auto countries = m_backend->countries(serverId);
        QVariantList updated;
        for (const auto &item : countries) {
            auto row = item.toMap();
            for (const auto &previous : m_rows) {
                if (previous.toMap().value("countryCode") == row.value("countryCode")) {
                    auto merged = previous.toMap();
                    for (auto it = row.begin(); it != row.end(); ++it) merged[it.key()] = it.value();
                    row = merged;
                    break;
                }
            }
            updated.append(row);
        }
        m_rows = updated;
    }
    emit changed();
}

void PremiumBenchmarkController::startAll() { begin(false); }
void PremiumBenchmarkController::checkCurrent() { if (canMeasure()) begin(true); }

void PremiumBenchmarkController::begin(bool single)
{
    if (!canStart()) return;
    m_single = single;
    m_originalCountry = currentCountry();
    m_originalProtocol = m_backend->protocol(m_serverId);
    m_originalConnected = m_backend->state() == BenchmarkBackend::Connected;
    m_restoring = m_stopping = false;
    m_cursor = 0;
    m_order.clear();
    if (!single) m_rows = m_backend->countries(m_serverId);
    for (const auto &item : m_rows) {
        const auto code = item.toMap().value("countryCode").toString();
        if (!code.isEmpty() && !m_order.contains(code) && (!single || code == m_originalCountry)) m_order.append(code);
    }
    if (m_order.isEmpty() || m_originalCountry.isEmpty()) return;
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    if (single && !m_reference.isNull()) { settle(); return; }
    setPhase(Resolving, 5000, tr("Resolving one Google address for the entire test..."));
    m_backend->resolveReference(++m_requestId, m_cancelled);
}

QString PremiumBenchmarkController::target() const
{
    return m_restoring ? m_originalCountry : m_order.value(m_cursor);
}

void PremiumBenchmarkController::setPhase(Phase phase, int timeoutMsecs, const QString &message)
{
    m_phase = phase;
    m_status = message;
    m_timeout.start(timeoutMsecs);
    emit changed();
}

void PremiumBenchmarkController::prepareTarget()
{
    if (m_backend->state() == BenchmarkBackend::Disconnected || m_backend->state() == BenchmarkBackend::Error) {
        loadTarget();
    } else {
        setPhase(Disconnecting, 12000, m_restoring ? tr("Restoring the original location...") : tr("Disconnecting before the next location..."));
        m_backend->disconnectVpn();
    }
}

void PremiumBenchmarkController::loadTarget()
{
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    setPhase(Loading, 60000, m_restoring ? tr("Restoring the original configuration...")
        : tr("Loading location %1 of %2: %3").arg(m_cursor + 1).arg(m_order.size()).arg(target()));
    m_backend->load(m_serverId, target(), m_restoring ? m_originalProtocol : QString(), ++m_requestId, m_cancelled);
}

void PremiumBenchmarkController::onLoaded(quint64 requestId, bool success, bool captcha)
{
    if (m_phase != Loading || requestId != m_requestId) return;
    if (m_stopping && !m_restoring) { restore(); return; }
    if (!success) {
        if (m_restoring) { finish(tr("Could not restore the original location. Reconnect manually.")); return; }
        if (captcha) { finish(tr("Test stopped: complete the Premium captcha and reconnect the original location manually.")); return; }
        record({{"state", "apiError"}});
        advance();
        return;
    }
    if (m_backend->country(m_serverId) != target()) {
        finish(tr("Test stopped: location changed unexpectedly")); return;
    }
    if (m_restoring && !m_originalConnected) {
        finish(m_stopping ? tr("Stopped. Original location restored.") : tr("Finished. Locations are sorted by average Google ping."));
        return;
    }
    setPhase(Connecting, 35000, m_restoring ? tr("Reconnecting the original location...")
        : tr("Connecting: %1").arg(target()));
    m_backend->connectVpn();
}

void PremiumBenchmarkController::onConnectionChanged()
{
    emit changed();
    if (!busy()) { refresh(m_serverId); return; }
    if (!m_backend->eligible(m_serverId)) {
        finish(tr("Test stopped: subscription or split tunneling settings changed"));
        return;
    }
    const auto state = m_backend->state();
    if (m_phase == Disconnecting && (state == BenchmarkBackend::Disconnected || state == BenchmarkBackend::Error)) loadTarget();
    else if (m_phase == Connecting && state == BenchmarkBackend::Connected) {
        if (m_restoring) finish(m_stopping ? tr("Stopped. Original connection restored.") : tr("Finished. Locations are sorted by average Google ping."));
        else settle();
    } else if (m_phase == Connecting && (state == BenchmarkBackend::Error || state == BenchmarkBackend::Disconnected)) {
        if (m_restoring) finish(tr("Original location restored, but reconnection failed."));
        else { record({{"state", "connectionError"}}); advance(); }
    } else if ((m_phase == Measuring || m_phase == Settling) && state != BenchmarkBackend::Connected) {
        finish(tr("Test stopped because the VPN connection changed"));
    }
}

void PremiumBenchmarkController::settle()
{
    setPhase(Settling, m_settleMsecs, tr("Waiting for the VPN route: %1").arg(target()));
}

void PremiumBenchmarkController::measure()
{
    if (m_backend->state() != BenchmarkBackend::Connected || currentCountry() != target()) {
        finish(tr("Test stopped: active location does not match the measurement")); return;
    }
    setPhase(Measuring, 10000, tr("Pinging Google: %1 (%2/%3)").arg(target()).arg(m_cursor + 1).arg(m_order.size()));
    auto *probe = m_factory(this);
    m_probe = probe;
    connect(probe, &BenchmarkProbe::finished, this, [this, probe](const QVariantMap &result) {
        if (m_phase != Measuring || m_probe != probe) return;
        m_probe = nullptr;
        probe->deleteLater();
        if (m_backend->state() != BenchmarkBackend::Connected || currentCountry() != target()) {
            finish(tr("Test stopped: active location changed")); return;
        }
        record(result);
        advance();
    });
    probe->start(m_reference);
}

void PremiumBenchmarkController::record(const QVariantMap &result)
{
    for (auto &item : m_rows) {
        auto row = item.toMap();
        if (row.value("countryCode").toString() != target()) continue;
        for (auto it = result.begin(); it != result.end(); ++it) row[it.key()] = it.value();
        row["protocol"] = m_backend->protocol(m_serverId);
        item = row;
        break;
    }
    emit changed();
}

void PremiumBenchmarkController::advance()
{
    if (m_single) { finish(tr("Measured. Results are sorted by average Google ping.")); return; }
    ++m_cursor;
    if (m_stopping || m_cursor >= m_order.size()) restore(); else prepareTarget();
}

void PremiumBenchmarkController::restore()
{
    m_restoring = true;
    prepareTarget();
}

void PremiumBenchmarkController::stop()
{
    if (!busy() || m_restoring) return;
    m_stopping = true;
    if (m_cancelled) m_cancelled->store(true);
    if (m_single || m_phase == Resolving) { finish(tr("Stopped")); return; }
    delete m_probe; m_probe = nullptr;
    if (m_phase == Loading) {
        m_status = tr("Stopping: waiting for the pending configuration request...");
        emit changed();
        return; // Serialize restoration after this request, never race key issuance.
    }
    restore();
}

void PremiumBenchmarkController::finish(const QString &message)
{
    if (m_cancelled) m_cancelled->store(true);
    m_timeout.stop();
    delete m_probe; m_probe = nullptr;
    m_phase = Idle;
    m_status = message;
    emit changed();
}

void PremiumBenchmarkController::onTimeout()
{
    if (m_phase == Settling) { measure(); return; }
    if (m_phase == Resolving) { finish(tr("Could not resolve google.com")); return; }
    if (m_phase == Loading) {
        m_stopping = true;
        if (m_cancelled) m_cancelled->store(true);
        m_status = tr("Request timed out: waiting for it to finish before restoration...");
        emit changed();
        return;
    }
    if (m_phase == Disconnecting) {
        finish(tr("Test stopped: operation timed out. Check the current connection manually.")); return;
    }
    if (m_restoring) { finish(tr("Original location could not reconnect. Reconnect manually.")); return; }
    delete m_probe; m_probe = nullptr;
    record({{"state", m_phase == Connecting ? "connectionError" : "timeout"}});
    advance();
}
