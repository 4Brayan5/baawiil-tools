#pragma once
#include <QString>
namespace features {
QString exportSettings(const QString &directory, const QString &destination);
QString importSettings(const QString &directory, const QString &source);
} // namespace features
