#include "googlePingProbe.h"
#include "pingStatistics.h"
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QNetworkInterface>
#include <QPromise>
#include <QtConcurrentRun>
#include <QtEndian>
#include <algorithm>
#include <numeric>
#include <thread>
#include <vector>
#include <functional>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <netioapi.h>
#endif

namespace {
QVariantMap measure(const QHostAddress &destination, const std::shared_ptr<std::atomic_bool> &cancelled,
                    const PingSettings &settings, const std::function<void(int)> &progress)
{
    QVariantMap result{{"state", "routeError"}, {"latencyMs", -1}, {"sent", 0}, {"replies", 0},
                       {"referenceAddress", destination.toString()}, {"method", "Google ICMP"}};
    if (settings.count < 1 || settings.count > 100 || settings.intervalMs < 0 || settings.intervalMs > 5000
        || settings.timeoutMs < 1 || settings.timeoutMs > 5000 || settings.warmupCount < 0 || settings.warmupCount > 3) {
        result["state"] = "pingError";
        return result;
    }
#ifdef Q_OS_WIN
    if (destination.protocol() != QAbstractSocket::IPv4Protocol) return result;
    QHostAddress source;
    int index = 0;
    for (const auto &adapter : QNetworkInterface::allInterfaces()) {
        if (!adapter.flags().testFlag(QNetworkInterface::IsUp)
            || adapter.humanReadableName().compare("AmneziaVPN", Qt::CaseInsensitive) != 0) continue;
        for (const auto &entry : adapter.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                source = entry.ip(); index = adapter.index(); break;
            }
        }
    }
    auto usesVpn = [&]() {
        MIB_UNICASTIPADDRESS_ROW local = {};
        InitializeUnicastIpAddressEntry(&local);
        local.InterfaceIndex = index;
        local.Address.Ipv4.sin_family = AF_INET;
        local.Address.Ipv4.sin_addr.s_addr = qToBigEndian(source.toIPv4Address());
        if (index <= 0 || source.isNull() || GetUnicastIpAddressEntry(&local) != NO_ERROR) return false;
        SOCKADDR_INET remote = {};
        remote.Ipv4.sin_family = AF_INET;
        remote.Ipv4.sin_addr.s_addr = qToBigEndian(destination.toIPv4Address());
        MIB_IPFORWARD_ROW2 route = {};
        SOCKADDR_INET selectedSource = {};
        return index > 0 && GetBestRoute2(nullptr, 0, nullptr, &remote, 0, &route, &selectedSource) == NO_ERROR
            && int(route.InterfaceIndex) == index;
    };
    if (source.isNull() || !usesVpn()) return result;
    result["sourceAddress"] = source.toString();
    result["interfaceIndex"] = index;
    HANDLE handle = IcmpCreateFile();
    if (handle == INVALID_HANDLE_VALUE) { result["state"] = "pingError"; return result; }
    char payload[32] = "Amnezia Google ping";
    std::vector<unsigned char> buffer(sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 128);
    QVector<int> observations;
    QVariantList elapsedSamples;
    const int total = settings.count + settings.warmupCount;
    for (int i = 0; i < total && !cancelled->load(); ++i) {
        if (!usesVpn()) { IcmpCloseHandle(handle); return result; }
        std::fill(buffer.begin(), buffer.end(), 0);
        const auto address = qToBigEndian(destination.toIPv4Address());
        QElapsedTimer elapsed;
        elapsed.start();
        const DWORD count = IcmpSendEcho2Ex(handle, nullptr, nullptr, nullptr, qToBigEndian(source.toIPv4Address()),
            address, payload, sizeof(payload), nullptr, buffer.data(), DWORD(buffer.size()), settings.timeoutMs);
        const auto *reply = reinterpret_cast<const ICMP_ECHO_REPLY *>(buffer.data());
        if (i >= settings.warmupCount) {
            if (count > 0 && reply->Status == IP_SUCCESS && reply->Address == address) {
                observations.append(int(reply->RoundTripTime));
                elapsedSamples.append(elapsed.elapsed());
            } else observations.append(-1);
        }
        if (!usesVpn()) { IcmpCloseHandle(handle); return result; }
        progress((std::max)(0, i + 1 - settings.warmupCount));
        // Start-to-start spacing; time spent waiting for a reply is included.
        while (i + 1 < total && !cancelled->load() && elapsed.elapsed() < settings.intervalMs) {
            const auto remaining = settings.intervalMs - elapsed.elapsed();
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min<qint64>(remaining, 50)));
        }
    }
    IcmpCloseHandle(handle);
    if (cancelled->load()) { result["state"] = "cancelled"; return result; }
    const auto statistics = PingStatistics::summarize(observations);
    for (auto it = statistics.begin(); it != statistics.end(); ++it) result[it.key()] = it.value();
    result["elapsedSamplesMs"] = elapsedSamples;
#else
    Q_UNUSED(cancelled)
    result["state"] = "unsupported";
#endif
    return result;
}
}

GooglePingProbe::GooglePingProbe(QObject *parent) : BenchmarkProbe(parent), m_cancelled(std::make_shared<std::atomic_bool>(false)) {}
GooglePingProbe::~GooglePingProbe() { m_cancelled->store(true); }
void GooglePingProbe::start(const QHostAddress &destination, const PingSettings &settings)
{
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        emit finished(result);
    });
    connect(watcher, &QFutureWatcher<QVariantMap>::progressValueChanged, this, [this, settings](int sent) {
        emit progress(sent, settings.count);
    });
    const auto cancelled = m_cancelled;
    watcher->setFuture(QtConcurrent::run([destination, cancelled, settings](QPromise<QVariantMap> &promise) {
        promise.setProgressRange(0, settings.count);
        promise.addResult(measure(destination, cancelled, settings, [&promise](int sent) { promise.setProgressValue(sent); }));
    }));
}
