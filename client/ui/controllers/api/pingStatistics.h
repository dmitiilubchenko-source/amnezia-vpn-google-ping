#ifndef PINGSTATISTICS_H
#define PINGSTATISTICS_H
#include <QVariantMap>
#include <QVector>

namespace PingStatistics {
// Chronological RTTs in milliseconds; a negative entry is a missed reply.
QVariantMap summarize(const QVector<int> &observations);
}
#endif
