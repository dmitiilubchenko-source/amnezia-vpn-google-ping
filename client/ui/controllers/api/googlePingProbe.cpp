#include "googlePingProbe.h"
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QNetworkInterface>
#include <QtConcurrentRun>
#include <QtEndian>
#include <algorithm>
#include <numeric>
#include <thread>
#include <vector>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <netioapi.h>
#endif

namespace {
QVariantMap measure(const QHostAddress &destination, const std::shared_ptr<std::atomic_bool> &cancelled)
{
    QVariantMap result{{"state", "routeError"}, {"latencyMs", -1}, {"sent", 5}, {"replies", 0},
                       {"loss", 100}, {"referenceAddress", destination.toString()}, {"method", "Google ICMP"}};
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
    std::vector<int> samples;
    QVariantList elapsedSamples;
    for (int i = 0; i < 5 && !cancelled->load(); ++i) {
        if (!usesVpn()) { IcmpCloseHandle(handle); return result; }
        std::fill(buffer.begin(), buffer.end(), 0);
        const auto address = qToBigEndian(destination.toIPv4Address());
        QElapsedTimer elapsed;
        elapsed.start();
        const DWORD count = IcmpSendEcho2Ex(handle, nullptr, nullptr, nullptr, qToBigEndian(source.toIPv4Address()),
            address, payload, sizeof(payload), nullptr, buffer.data(), DWORD(buffer.size()), 1000);
        const auto *reply = reinterpret_cast<const ICMP_ECHO_REPLY *>(buffer.data());
        if (count > 0 && reply->Status == IP_SUCCESS && reply->Address == address) {
            samples.push_back(int(reply->RoundTripTime));
            elapsedSamples.append(elapsed.elapsed());
        }
        if (!usesVpn()) { IcmpCloseHandle(handle); return result; }
        if (i < 4 && !cancelled->load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    IcmpCloseHandle(handle);
    if (cancelled->load()) { result["state"] = "cancelled"; return result; }
    result["replies"] = int(samples.size());
    result["loss"] = (5 - int(samples.size())) * 20;
    if (samples.empty()) { result["state"] = "timeout"; return result; }
    std::sort(samples.begin(), samples.end());
    result["state"] = "ready";
    result["latencyMs"] = int((std::accumulate(samples.begin(), samples.end(), 0) + samples.size() / 2) / samples.size());
    result["minMs"] = samples.front();
    result["maxMs"] = samples.back();
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
void GooglePingProbe::start(const QHostAddress &destination)
{
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        emit finished(result);
    });
    watcher->setFuture(QtConcurrent::run(measure, destination, m_cancelled));
}
