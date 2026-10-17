#ifndef TUNNELLATENCYPROBE_H
#define TUNNELLATENCYPROBE_H

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QSslSocket>
#include <QTimer>
#include <QVariantMap>

// HTTPS response RTT on one warmed TLS connection, pinned to the VPN interface.
class TunnelLatencyProbe : public QObject
{
    Q_OBJECT
public:
    explicit TunnelLatencyProbe(QObject *parent = nullptr);
    void start();
signals:
    void finished(const QVariantMap &measurement);
private:
    void sendRequest();
    void readResponse();
    void complete(const QString &state);
    bool routeUsesTunnel() const;
    QSslSocket m_socket;
    QTimer m_timeout;
    QElapsedTimer m_elapsed;
    QHostAddress m_source;
    int m_interfaceIndex = 0;
    QByteArray m_headers;
    QList<int> m_samples;
    bool m_warmed = false;
    bool m_done = false;
};

#endif
