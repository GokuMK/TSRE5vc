#pragma once
#include "ContentCase.h"
#include <QMap>
namespace ContentCase {
// Journal directories must be new and outside the game root. A false continuation
// leaves an interrupted journal for recovery; production callers omit this hook.
QJsonObject execute(const QString &root, const QJsonObject &savedPlan,
                    const QString &journalDirectory, QString &error,
                    const std::function<void(const QString &)> &progress = {},
                    const std::function<bool(int)> &continuation = {});
bool rollback(const QString &root, const QString &journalPath, QString &error,
              const std::function<void(const QString &)> &progress = {});
bool verifyReferences(const QJsonObject &before, const QJsonObject &after,
                      const QMap<QString, QString> &paths, QString &error);
} // namespace ContentCase
