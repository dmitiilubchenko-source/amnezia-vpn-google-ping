#include "serverLatencyProbe.h"

#include <QFutureWatcher>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QTimer>
#include <QtConcurrentRun>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <vector>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#endif

namespace {
QVariantMap result(const QString &state, int milliseconds = -1, int replies = 0)
{
    return {{"state", state}, {"latencyMs", milliseconds}, {"replies", replies}};
}

QVariantMap measureAddress(const QHostAddress &address, const std::shared_ptr<std::atomic_bool> &cancelled)
{
#ifdef Q_OS_WIN
    const bool ipv6 = address.protocol() == QAbstractSocket::IPv6Protocol;
    HANDLE handle = ipv6 ? Icmp6CreateFile() : IcmpCreateFile();
    if (handle == INVALID_HANDLE_VALUE) {
        return result("error");
    }

    char payload[] = "Amnezia latency";
    // Include the ICMP error bytes required by the Windows API. vector storage
    // is suitably aligned for either reply structure.
    std::vector<unsigned char> buffer(sizeof(ICMP_ECHO_REPLY) + sizeof(ICMPV6_ECHO_REPLY) + sizeof(payload) + 64);
    std::vector<int> samples;
    for (int i = 0; i < 3 && !cancelled->load(); ++i) {
        std::fill(buffer.begin(), buffer.end(), 0);
        DWORD count = 0;
        if (ipv6) {
            sockaddr_in6 source = {};
            sockaddr_in6 destination = {};
            source.sin6_family = AF_INET6;
            destination.sin6_family = AF_INET6;
            const auto bytes = address.toIPv6Address();
            std::memcpy(&destination.sin6_addr, bytes.c, sizeof(bytes.c));
            bool numericScope = false;
            destination.sin6_scope_id = address.scopeId().toUInt(&numericScope);
            if (!numericScope && !address.scopeId().isEmpty()) {
                destination.sin6_scope_id = QNetworkInterface::interfaceIndexFromName(address.scopeId());
            }
            count = Icmp6SendEcho2(handle, nullptr, nullptr, nullptr, &source, &destination,
                                  payload, sizeof(payload), nullptr, buffer.data(), DWORD(buffer.size()), 1000);
            const auto *reply = reinterpret_cast<const ICMPV6_ECHO_REPLY *>(buffer.data());
            if (count > 0 && reply->Status == IP_SUCCESS) {
                samples.push_back(int(reply->RoundTripTime));
            }
        } else {
            const auto destination = qToBigEndian(address.toIPv4Address());
            count = IcmpSendEcho(handle, destination, payload, sizeof(payload), nullptr,
                                 buffer.data(), DWORD(buffer.size()), 1000);
            const auto *reply = reinterpret_cast<const ICMP_ECHO_REPLY *>(buffer.data());
            // An unreachable/TTL-exceeded reply must never become a low ping.
            if (count > 0 && reply->Status == IP_SUCCESS && reply->Address == destination) {
                samples.push_back(int(reply->RoundTripTime));
            }
        }
    }
    IcmpCloseHandle(handle);
    if (cancelled->load()) {
        return result("cancelled");
    }
    if (samples.empty()) {
        return result("timeout");
    }
    std::sort(samples.begin(), samples.end());
    const int middle = int(samples.size()) / 2;
    const int median = samples.size() == 2 ? (samples[0] + samples[1]) / 2 : samples[middle];
    return result("ready", median, int(samples.size()));
#else
    Q_UNUSED(address)
    Q_UNUSED(cancelled)
    return result("unsupported");
#endif
}
}

ServerLatencyProbe::ServerLatencyProbe(QObject *parent)
    : QObject(parent), m_cancelled(std::make_shared<std::atomic_bool>(false))
{
}

ServerLatencyProbe::~ServerLatencyProbe()
{
    m_cancelled->store(true);
    if (m_lookupId >= 0) {
        QHostInfo::abortHostLookup(m_lookupId);
    }
}

void ServerLatencyProbe::start(const QString &host)
{
    if (host.trimmed().isEmpty()) {
        QTimer::singleShot(0, this, [this]() { complete(result("dnsError")); });
        return;
    }
    const QHostAddress address(host);
    if (!address.isNull()) {
        measure(address);
        return;
    }
    m_lookupId = QHostInfo::lookupHost(host, this, [this](const QHostInfo &info) {
        if (m_finished) {
            return;
        }
        m_lookupId = -1;
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            complete(result("dnsError"));
            return;
        }
        // Prefer IPv4, matching the common Premium endpoint configuration.
        for (const auto &address : info.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                measure(address);
                return;
            }
        }
        measure(info.addresses().first());
    });
    QTimer::singleShot(4000, this, [this]() {
        if (m_lookupId >= 0 && !m_finished) {
            QHostInfo::abortHostLookup(m_lookupId);
            m_lookupId = -1;
            complete(result("dnsError"));
        }
    });
}

void ServerLatencyProbe::measure(const QHostAddress &address)
{
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher]() {
        const auto measurement = watcher->result();
        watcher->deleteLater();
        complete(measurement);
    });
    watcher->setFuture(QtConcurrent::run(measureAddress, address, m_cancelled));
}

void ServerLatencyProbe::complete(const QVariantMap &measurement)
{
    if (!m_finished) {
        m_finished = true;
        emit finished(measurement);
    }
}
