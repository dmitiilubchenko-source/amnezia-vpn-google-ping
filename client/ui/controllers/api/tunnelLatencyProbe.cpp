#include "tunnelLatencyProbe.h"

#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QtEndian>
#include <algorithm>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#endif

namespace {
const QHostAddress destination("1.1.1.1");
}

TunnelLatencyProbe::TunnelLatencyProbe(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this]() { complete("timeout"); });
    connect(&m_socket, &QSslSocket::encrypted, this, &TunnelLatencyProbe::sendRequest);
    connect(&m_socket, &QSslSocket::readyRead, this, &TunnelLatencyProbe::readResponse);
    connect(&m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &) { complete("tlsError"); });
    connect(&m_socket, &QSslSocket::errorOccurred, this, [this]() { complete("networkError"); });
    connect(&m_socket, &QSslSocket::disconnected, this, [this]() { complete("networkError"); });
}

bool TunnelLatencyProbe::routeUsesTunnel() const
{
#ifdef Q_OS_WIN
    SOCKADDR_INET remote = {};
    remote.Ipv4.sin_family = AF_INET;
    remote.Ipv4.sin_addr.s_addr = qToBigEndian(destination.toIPv4Address());
    MIB_IPFORWARD_ROW2 route = {};
    SOCKADDR_INET source = {};
    return GetBestRoute2(nullptr, 0, nullptr, &remote, 0, &route, &source) == NO_ERROR
        && int(route.InterfaceIndex) == m_interfaceIndex;
#else
    return false;
#endif
}

void TunnelLatencyProbe::start()
{
    // Defer failures too, so all deliveries have the same asynchronous contract.
    QTimer::singleShot(0, this, [this]() {
        for (const auto &adapter : QNetworkInterface::allInterfaces()) {
            if (!adapter.flags().testFlag(QNetworkInterface::IsUp)
                || adapter.humanReadableName().compare("AmneziaVPN", Qt::CaseInsensitive) != 0) {
                continue;
            }
            for (const auto &entry : adapter.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    m_source = entry.ip();
                    m_interfaceIndex = adapter.index();
                    break;
                }
            }
        }
        if (m_source.isNull() || !routeUsesTunnel()) {
            complete("routeError");
            return;
        }
        m_socket.setProxy(QNetworkProxy::NoProxy);
        if (!m_socket.bind(m_source)) {
            complete("routeError");
            return;
        }
#ifdef Q_OS_WIN
        // Source binding alone is insufficient on a Windows weak-host stack.
        // Pin the outgoing interface as well. Never retry on a physical adapter.
        const DWORD index = qToBigEndian(quint32(m_interfaceIndex));
        if (setsockopt(SOCKET(m_socket.socketDescriptor()), IPPROTO_IP, IP_UNICAST_IF,
                       reinterpret_cast<const char *>(&index), sizeof(index)) != 0) {
            complete("routeError");
            return;
        }
#endif
        m_socket.setPeerVerifyName("one.one.one.one");
        m_timeout.start(8000);
        m_socket.connectToHostEncrypted(destination.toString(), 443);
    });
}

void TunnelLatencyProbe::sendRequest()
{
    if (m_done) return;
    if (!routeUsesTunnel()) {
        complete("routeError");
        return;
    }
    m_headers.clear();
    m_elapsed.start();
    m_timeout.start(5000);
    m_socket.write("HEAD / HTTP/1.1\r\nHost: one.one.one.one\r\n"
                   "Connection: keep-alive\r\nCache-Control: no-cache\r\n\r\n");
}

void TunnelLatencyProbe::readResponse()
{
    if (m_done) return;
    m_headers += m_socket.readAll();
    if (m_headers.size() > 16384) {
        complete("httpError");
        return;
    }
    if (!m_headers.contains("\r\n\r\n")) return;
    const int status = m_headers.left(m_headers.indexOf("\r\n")).split(' ').value(1).toInt();
    if (status < 200 || status >= 300) {
        complete("httpError");
        return;
    }
    if (!routeUsesTunnel() || m_socket.localAddress() != m_source) {
        complete("routeError");
        return;
    }
    m_timeout.stop();
    // Discard the warm-up request; TLS negotiation is outside the measured RTT.
    if (m_warmed) m_samples.append(int(m_elapsed.elapsed()));
    m_warmed = true;
    if (m_samples.size() == 3) {
        complete("ready");
    } else {
        QTimer::singleShot(150, this, &TunnelLatencyProbe::sendRequest);
    }
}

void TunnelLatencyProbe::complete(const QString &state)
{
    if (m_done) return;
    m_done = true;
    m_timeout.stop();
    m_socket.abort();
    QVariantMap result{{"state", state}, {"latencyMs", -1}, {"replies", m_samples.size()},
                       {"target", "Cloudflare 1.1.1.1"}, {"method", "HTTPS"}};
    if (state == "ready") {
        std::sort(m_samples.begin(), m_samples.end());
        result["latencyMs"] = m_samples.at(1);
        result["minMs"] = m_samples.first();
        result["maxMs"] = m_samples.last();
    }
    emit finished(result);
}
