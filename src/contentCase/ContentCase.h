#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>

namespace ContentCase {
// Read-only scanner. No API in this module performs content mutation.
QJsonObject scan(const QString &root, QString &error,
                 const std::function<void(const QString &)> &progress = {});
QString markdownReport(const QJsonObject &plan);
int run(int argc, char **argv);
}
