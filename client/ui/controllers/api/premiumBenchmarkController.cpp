#include "premiumBenchmarkController.h"
#include <algorithm>
#include <QDateTime>
#include <QUuid>
#include <cmath>

namespace {
const QStringList resultKeys{"countryCode", "state", "protocol", "measurementStage", "latencyMs", "medianMs", "p95Ms",
    "jitterMs", "loss", "sent", "replies", "referenceAddress", "checkedAtUtc", "runId"};
bool stale(const QVariantMap &row)
{
    const auto date = QDateTime::fromString(row.value("checkedAtUtc").toString(), Qt::ISODate);
    const auto age = date.secsTo(QDateTime::currentDateTimeUtc());
    return !date.isValid() || age < -300 || age > 24 * 60 * 60;
}
}

PremiumBenchmarkController::PremiumBenchmarkController(BenchmarkBackend *backend, QObject *parent, ProbeFactory factory,
                                                       int settleMsecs, int loadTimeoutMsecs)
    : QObject(parent), m_backend(backend), m_factory(factory), m_settleMsecs(settleMsecs), m_loadTimeoutMsecs(loadTimeoutMsecs)
{
    if (!m_factory) m_factory = [](QObject *parent) { return new GooglePingProbe(parent); };
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &PremiumBenchmarkController::onTimeout);
    m_progressTimer.setInterval(1000);
    connect(&m_progressTimer, &QTimer::timeout, this, &PremiumBenchmarkController::progressChanged);
    connect(this, &PremiumBenchmarkController::changed, this, &PremiumBenchmarkController::progressChanged);
    m_expiryTimer.setInterval(60000);
    connect(&m_expiryTimer, &QTimer::timeout, this, &PremiumBenchmarkController::changed);
    m_expiryTimer.start();
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
    for (auto &item : rows) {
        auto row = item.toMap();
        const auto code = row.value("countryCode").toString();
        row["favorite"] = m_favorites.contains(code);
        row["saved"] = m_restored.contains(code);
        row["stale"] = !row.value("state").toString().isEmpty() && stale(row);
        row["historical"] = !row.value("state").toString().isEmpty()
            && (row.value("runId").toString() != m_latestRunId || row.value("stale").toBool());
        item = row;
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        const auto left = a.toMap(), right = b.toMap();
        const auto group = [](const QVariantMap &row) {
            return row.value("state").toString().isEmpty() ? 2 : row.value("historical").toBool() ? 1 : 0;
        };
        if (group(left) != group(right)) return group(left) < group(right);
        const bool lready = left.value("state") == "ready", rready = right.value("state") == "ready";
        if (lready != rready) return lready;
        if (!lready) return false;
        const bool ldetailed = left.value("measurementStage") == "detailed", rdetailed = right.value("measurementStage") == "detailed";
        if (ldetailed != rdetailed) return ldetailed;
        // Separate evidence levels, then compare loss and slow replies before typical RTT.
        for (const auto *key : {"loss", "p95Ms", "medianMs", "jitterMs"}) {
            const double l = left.value(key, left.value("latencyMs", -1)).toDouble();
            const double r = right.value(key, right.value("latencyMs", -1)).toDouble();
            if (l != r) return (l < 0 ? 1e12 : l) < (r < 0 ? 1e12 : r);
        }
        return false;
    });
    return rows;
}

QVariantMap PremiumBenchmarkController::recommendation() const
{
    if (busy()) return {};
    for (const auto &item : ranking()) {
        const auto row = item.toMap();
        if (row.value("state") == "ready" && row.value("measurementStage") == "detailed"
            && row.value("runId").toString() == m_latestRunId && !row.value("stale").toBool()
            && row.value("sent").toInt() >= 30 && row.value("replies").toInt() >= 3) return row;
    }
    return {};
}

double PremiumBenchmarkController::overallProgress() const
{
    if (!m_totalVisits) return 0;
    const double partial = m_phase == Measuring ? double(m_probeSent) / probeSettings().count : 0;
    return std::clamp((m_completedVisits + partial) / m_totalVisits, 0.0, 1.0);
}

int PremiumBenchmarkController::estimatedSecondsRemaining() const
{
    if (!busy() || m_restoring || m_stopping) return -1;
    const double quick = m_connectionSeconds + 1.2, detailed = m_connectionSeconds + 31;
    const double currentBudget = m_detailed ? detailed : quick;
    const double elapsed = m_visitClock.isValid() ? m_visitClock.elapsed() / 1000.0 : 0;
    if (elapsed > currentBudget + 5 && m_phase != Measuring) return -1;
    double currentRemaining = std::max(1.0, currentBudget - elapsed);
    if (m_phase == Measuring) currentRemaining = std::max(1, probeSettings().count - m_probeSent) * (m_detailed ? 1.0 : 0.2);
    const int queued = std::max(0, int(m_order.size()) - m_cursor - 1);
    return int(std::ceil(currentRemaining + queued * (m_detailed ? detailed : quick)
        + (!m_detailed ? m_expectedDetailed * detailed : 0) + (!m_single ? m_connectionSeconds : 0)));
}

QString PremiumBenchmarkController::progressText() const
{
    if (!m_totalVisits) return {};
    if (m_restoring && busy()) return tr("Checked %1/%2. Restoring the original connection...").arg(m_completedVisits).arg(m_totalVisits);
    const auto base = tr("Checked %1 of %2 locations").arg(m_completedVisits).arg(m_totalVisits);
    const int seconds = estimatedSecondsRemaining();
    if (seconds < 0) return base;
    return base + (seconds >= 60 ? tr(" · about %1 min remaining").arg((seconds + 59) / 60)
                                : tr(" · about %1 sec remaining").arg(((seconds + 9) / 10) * 10));
}

void PremiumBenchmarkController::loadSaved()
{
    m_favorites.clear(); m_restored.clear(); m_latestRunId.clear();
    const auto saved = m_backend->preferences(m_serverId);
    if (saved.value("version").toInt() != 1) return;
    m_latestRunId = saved.value("lastRunId").toString();
    const auto favorites = saved.value("favorites").toStringList();
    for (auto &item : m_rows) {
        auto row = item.toMap();
        const auto code = row.value("countryCode").toString();
        if (favorites.contains(code)) m_favorites.insert(code);
        for (const auto &stored : saved.value("rows").toList()) {
            const auto previous = stored.toMap();
            if (previous.value("countryCode").toString() != code || previous.value("runId").toString().isEmpty()
                || !QDateTime::fromString(previous.value("checkedAtUtc").toString(), Qt::ISODate).isValid()) continue;
            if (previous.value("state") == "ready") {
                const int sent = previous.value("sent").toInt(), replies = previous.value("replies").toInt();
                if (sent < 1 || sent > 100 || replies < 1 || replies > sent) continue;
                bool valid = true;
                for (const auto *key : {"medianMs", "p95Ms", "loss"}) {
                    const double value = previous.value(key, -1).toDouble();
                    if (!std::isfinite(value) || value < 0) valid = false;
                }
                if (!valid || previous.value("loss").toDouble() > 100) continue;
            }
            for (const auto &key : resultKeys) if (previous.contains(key)) row[key] = previous.value(key);
            m_restored.insert(code);
            break;
        }
        item = row;
    }
}

void PremiumBenchmarkController::save()
{
    if (m_serverId.isEmpty()) return;
    QVariantList rows;
    for (const auto &item : m_rows) {
        const auto row = item.toMap();
        if (row.value("state").toString().isEmpty()) continue;
        QVariantMap result;
        for (const auto &key : resultKeys) if (row.contains(key)) result[key] = row.value(key);
        rows.append(result);
    }
    QStringList favorites(m_favorites.begin(), m_favorites.end());
    favorites.sort();
    m_backend->savePreferences(m_serverId, {{"version", 1}, {"favorites", favorites}, {"rows", rows}, {"lastRunId", m_latestRunId}});
}

void PremiumBenchmarkController::mergeCountries()
{
    QVariantList updated;
    QSet<QString> available;
    for (const auto &item : m_backend->countries(m_serverId)) {
        auto row = item.toMap();
        const auto code = row.value("countryCode").toString();
        available.insert(code);
        for (const auto &previous : m_rows) {
            if (previous.toMap().value("countryCode").toString() != code) continue;
            auto merged = previous.toMap();
            for (auto it = row.begin(); it != row.end(); ++it) merged[it.key()] = it.value();
            row = merged; break;
        }
        updated.append(row);
    }
    m_rows = updated;
    m_favorites.intersect(available);
    m_restored.intersect(available);
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
        loadSaved();
        m_reference = {};
        m_status.clear();
        m_completedVisits = m_totalVisits = 0;
    } else if (!busy()) {
        mergeCountries();
    }
    emit changed();
}

void PremiumBenchmarkController::startAll() { begin(AllCountries); }
void PremiumBenchmarkController::checkCurrent() { if (canMeasure()) begin(CurrentCountry); }
void PremiumBenchmarkController::startFavorites() { if (!m_favorites.isEmpty()) begin(FavoriteCountries); }
void PremiumBenchmarkController::checkCountry(const QString &code)
{
    if (canMeasure() && code == currentCountry()) checkCurrent(); else begin(OneCountry, code);
}

void PremiumBenchmarkController::toggleFavorite(const QString &code)
{
    if (busy()) return;
    bool available = false;
    for (const auto &item : m_rows) if (item.toMap().value("countryCode").toString() == code) available = true;
    if (!available) return;
    if (m_favorites.contains(code)) m_favorites.remove(code); else m_favorites.insert(code);
    save();
    emit changed();
}

void PremiumBenchmarkController::begin(Mode mode, const QString &countryCode)
{
    if (!canStart()) return;
    mergeCountries();
    m_single = mode == CurrentCountry;
    m_detailed = mode != AllCountries;
    m_originalCountry = currentCountry();
    m_originalProtocol = m_backend->protocol(m_serverId);
    m_originalConnected = m_backend->state() == BenchmarkBackend::Connected;
    m_restoring = m_stopping = false;
    m_cursor = 0;
    m_order.clear();
    for (const auto &item : m_rows) {
        const auto code = item.toMap().value("countryCode").toString();
        const bool selected = mode == AllCountries || (mode == CurrentCountry && code == m_originalCountry)
            || (mode == OneCountry && code == countryCode) || (mode == FavoriteCountries && m_favorites.contains(code));
        if (!code.isEmpty() && !m_order.contains(code) && selected) m_order.append(code);
    }
    if (m_order.isEmpty() || m_originalCountry.isEmpty()) return;
    m_runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_completedVisits = m_probeSent = 0;
    m_expectedDetailed = m_detailed ? 0 : std::min(5, int(m_order.size()));
    m_totalVisits = int(m_order.size()) + m_expectedDetailed;
    m_visitClock.restart();
    m_progressTimer.start();
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    setPhase(Resolving, 5000, tr("Resolving one Google address for the entire test..."));
    m_backend->resolveReference(++m_requestId, m_cancelled);
}

QString PremiumBenchmarkController::target() const
{
    return m_restoring ? m_originalCountry : m_order.value(m_cursor);
}

QString PremiumBenchmarkController::targetName() const
{
    for (const auto &item : m_rows) {
        const auto row = item.toMap();
        if (row.value("countryCode").toString() == target()) return row.value("countryName", target()).toString();
    }
    return target();
}

PingSettings PremiumBenchmarkController::probeSettings() const
{
    return m_detailed ? PingSettings{30, 1000, 1000} : PingSettings{5, 200, 1000};
}

QString PremiumBenchmarkController::completionMessage() const
{
    return tr("Finished. Detailed results first, then quick estimates; sorted by loss, P95 and median.");
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
    m_visitClock.restart();
    m_probeSent = 0;
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
    setPhase(Loading, m_loadTimeoutMsecs, m_restoring ? tr("Restoring the original configuration...")
        : tr("Loading location %1 of %2: %3").arg(m_cursor + 1).arg(m_order.size()).arg(target()));
    QString preferred = m_originalProtocol;
    if (m_detailed && !m_restoring) {
        for (const auto &item : m_rows) {
            const auto row = item.toMap();
            if (row.value("countryCode").toString() == target()) preferred = row.value("protocol", preferred).toString();
        }
    }
    m_backend->load(m_serverId, target(), preferred, ++m_requestId, m_cancelled);
}

void PremiumBenchmarkController::onLoaded(quint64 requestId, bool success, bool captcha, bool timedOut)
{
    if (m_phase != Loading || requestId != m_requestId) return;
    if (timedOut) {
        finish(tr("Configuration request timed out. Select the original location and reconnect manually."));
        return;
    }
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
        finish(m_stopping ? tr("Stopped. Original location restored.") : completionMessage());
        return;
    }
    // Real AWG handshakes can take a minute. Do not tear the tunnel down
    // before a slow but healthy connection has a chance to complete.
    m_connectionAttemptStarted = false;
    setPhase(Connecting, 90000, m_restoring ? tr("Reconnecting the original location...")
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
        if (m_restoring) finish(m_stopping ? tr("Stopped. Original connection restored.") : completionMessage());
        else settle();
    } else if (m_phase == Connecting && state == BenchmarkBackend::Connecting) {
        m_connectionAttemptStarted = true;
    // Ignore a pre-existing Disconnected notification until the queued
    // connection attempt starts. Immediate backend errors still fail normally.
    } else if (m_phase == Connecting && (state == BenchmarkBackend::Error
               || (state == BenchmarkBackend::Disconnected && m_connectionAttemptStarted))) {
        if (m_restoring) finish(tr("Original location restored, but reconnection failed."));
        else { record({{"state", "connectionError"}}); advance(); }
    } else if ((m_phase == Measuring || m_phase == Settling)
               && (state != BenchmarkBackend::Connected || currentCountry() != target()
                   || m_backend->protocol(m_serverId) != m_measureProtocol)) {
        connectionChangedDuringMeasurement();
    }
}

void PremiumBenchmarkController::connectionChangedDuringMeasurement()
{
    // A manual country/protocol change belongs to the user. A dropped tunnel
    // during a switching test must still attempt to return to the initial state.
    if (m_single || currentCountry() != target() || m_backend->protocol(m_serverId) != m_measureProtocol) {
        finish(tr("Test stopped because the VPN connection changed"));
        return;
    }
    delete m_probe; m_probe = nullptr;
    record({{"state", "connectionError"}});
    m_stopping = true;
    restore();
}

void PremiumBenchmarkController::settle()
{
    m_measureProtocol = m_backend->protocol(m_serverId);
    setPhase(Settling, m_settleMsecs, tr("Waiting for the VPN route: %1").arg(targetName()));
}

void PremiumBenchmarkController::measure()
{
    if (m_backend->state() != BenchmarkBackend::Connected || currentCountry() != target()
        || m_backend->protocol(m_serverId) != m_measureProtocol) {
        connectionChangedDuringMeasurement(); return;
    }
    const auto settings = probeSettings();
    if (m_visitClock.isValid() && !m_single) {
        const double overhead = m_visitClock.elapsed() / 1000.0;
        m_connectionSeconds = std::clamp(0.5 * m_connectionSeconds + 0.5 * overhead, 1.0, 60.0);
    }
    setPhase(Measuring, settings.count * std::max(settings.intervalMs, settings.timeoutMs) + 5000,
        m_detailed ? tr("Detailed check: %1 · 0/30 requests").arg(targetName()) : tr("Quick check: %1 · 0/5 requests").arg(targetName()));
    auto *probe = m_factory(this);
    m_probe = probe;
    connect(probe, &BenchmarkProbe::progress, this, [this, probe](int sent, int total) {
        if (m_phase != Measuring || m_probe != probe) return;
        m_probeSent = sent;
        m_status = (m_detailed ? tr("Detailed check: %1 · %2/%3 requests") : tr("Quick check: %1 · %2/%3 requests"))
            .arg(targetName()).arg(sent).arg(total);
        emit changed();
    });
    connect(probe, &BenchmarkProbe::finished, this, [this, probe](const QVariantMap &result) {
        if (m_phase != Measuring || m_probe != probe) return;
        m_probe = nullptr;
        probe->deleteLater();
        if (m_backend->state() != BenchmarkBackend::Connected || currentCountry() != target()
            || m_backend->protocol(m_serverId) != m_measureProtocol) {
            connectionChangedDuringMeasurement(); return;
        }
        record(result);
        advance();
    });
    probe->start(m_reference, settings);
}

void PremiumBenchmarkController::record(const QVariantMap &result)
{
    for (auto &item : m_rows) {
        auto row = item.toMap();
        if (row.value("countryCode").toString() != target()) continue;
        row = {{"countryCode", target()}, {"countryName", row.value("countryName")}, {"countryImageCode", row.value("countryImageCode")}};
        for (auto it = result.begin(); it != result.end(); ++it) row[it.key()] = it.value();
        row["protocol"] = m_backend->protocol(m_serverId);
        row["measurementStage"] = m_detailed ? "detailed" : "quick";
        row["checkedAtUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        row["runId"] = m_runId;
        m_latestRunId = m_runId;
        m_restored.remove(target());
        item = row;
        break;
    }
    save();
    emit changed();
}

void PremiumBenchmarkController::advance()
{
    ++m_completedVisits;
    m_probeSent = 0;
    if (m_single) { finish(tr("Current location measured: 30 requests. Detailed results are shown first.")); return; }
    ++m_cursor;
    if (m_stopping) { restore(); return; }
    if (m_cursor >= m_order.size()) {
        if (!m_detailed && beginDetailedPass()) return;
        restore();
    } else prepareTarget();
}

bool PremiumBenchmarkController::beginDetailedPass()
{
    QStringList candidates;
    for (const auto &item : ranking()) {
        const auto row = item.toMap();
        if (row.value("state") != "ready" || row.value("runId").toString() != m_runId) continue;
        candidates.append(row.value("countryCode").toString());
        if (candidates.size() == 5) break;
    }
    m_totalVisits = m_completedVisits + int(candidates.size());
    if (candidates.isEmpty()) return false;
    m_detailed = true;
    m_order = candidates;
    m_cursor = 0;
    prepareTarget();
    return true;
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
    m_progressTimer.stop();
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
        // The backend has its own total deadline. This watchdog also bounds a
        // broken backend; canStart remains false while a request is outstanding.
        finish(tr("Configuration request timed out. Select the original location and reconnect manually."));
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
