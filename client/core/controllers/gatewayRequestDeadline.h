#ifndef GATEWAYREQUESTDEADLINE_H
#define GATEWAYREQUESTDEADLINE_H

#include <QNetworkReply>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <functional>
#include <utility>

// A deadline for one gateway operation, including all of its fallback replies.
class GatewayRequestDeadline : public QObject {
public:
    explicit GatewayRequestDeadline(QObject *parent = nullptr) : QObject(parent)
    {
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            m_expired = true;
            QList<QPointer<QNetworkReply>> replies;
            for (auto *reply : m_replies) replies.append(reply);
            for (const auto &reply : replies) if (reply) reply->abort();
            auto callback = std::exchange(m_onExpired, {});
            if (callback) callback();
        });
    }

    bool expired() const { return m_expired; }
    void start(int timeoutMsecs, std::function<void()> onExpired)
    {
        m_onExpired = std::move(onExpired);
        m_timer.start(timeoutMsecs);
    }
    void complete()
    {
        m_timer.stop();
        m_onExpired = {};
    }
    void track(QNetworkReply *reply)
    {
        // A queued fallback must not outlive the operation's deadline.
        if (m_expired) { reply->abort(); reply->deleteLater(); return; }
        m_replies.insert(reply);
        connect(reply, &QObject::destroyed, this, [this, reply]() { m_replies.remove(reply); });
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_replies.remove(reply);
            if (m_expired) reply->deleteLater();
        });
    }

private:
    QTimer m_timer;
    QSet<QNetworkReply *> m_replies;
    std::function<void()> m_onExpired;
    bool m_expired = false;
};

#endif
