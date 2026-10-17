#include "pingStatistics.h"
#include <algorithm>
#include <cmath>
#include <numeric>

QVariantMap PingStatistics::summarize(const QVector<int> &observations)
{
    QVector<int> successful;
    QVariantList raw;
    double adjacentDifference = 0;
    int pairs = 0;
    for (qsizetype i = 0; i < observations.size(); ++i) {
        const int rtt = observations[i];
        raw.append(rtt);
        if (rtt < 0) continue;
        successful.append(rtt);
        if (i > 0 && observations[i - 1] >= 0) {
            adjacentDifference += std::abs(double(rtt) - observations[i - 1]);
            ++pairs;
        }
    }
    const int sent = int(observations.size()), replies = int(successful.size());
    QVariantMap result{{"state", replies ? "ready" : "timeout"}, {"sent", sent}, {"replies", replies},
        {"loss", sent ? 100.0 * (sent - replies) / sent : -1.0}, {"latencyMs", -1}, {"medianMs", -1},
        {"p95Ms", -1}, {"jitterMs", pairs ? adjacentDifference / pairs : -1.0},
        {"jitterPairs", pairs}, {"observationsMs", raw}};
    if (!replies) return result;
    std::sort(successful.begin(), successful.end());
    result["latencyMs"] = std::accumulate(successful.begin(), successful.end(), 0.0) / replies;
    result["medianMs"] = replies % 2 ? double(successful[replies / 2])
        : (double(successful[replies / 2 - 1]) + successful[replies / 2]) / 2;
    result["p95Ms"] = successful[int(std::ceil(0.95 * replies)) - 1];
    result["minMs"] = successful.front();
    result["maxMs"] = successful.back();
    return result;
}
