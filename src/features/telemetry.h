#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <cmath>
namespace features {
struct TelemetryBatch {
    bool valid = false;
    qint64 sourceMonotonic = 0, sourceUtc = 0;
    QMap<QString, double> values;
};
inline TelemetryBatch decodeTelemetry(const QJsonObject &packet, qint64 observedMonotonic) {
    TelemetryBatch batch;
    if (!packet["capturedMonotonicMs"].isDouble() || !packet["capturedUtcMs"].isDouble())
        return batch;
    batch.sourceMonotonic = packet["capturedMonotonicMs"].toInteger(-1);
    batch.sourceUtc = packet["capturedUtcMs"].toInteger();
    if (batch.sourceMonotonic < 0 || batch.sourceMonotonic > observedMonotonic)
        return batch;
    batch.valid = true;
    auto rows = packet["rows"].toArray();
    if (rows.isEmpty())
        return batch;
    auto values = rows[0].toObject();
    for (auto it = values.begin(); it != values.end(); ++it)
        if (it.value().isDouble() && std::isfinite(it.value().toDouble()))
            batch.values[it.key()] = it.value().toDouble();
    return batch;
}
} // namespace features
