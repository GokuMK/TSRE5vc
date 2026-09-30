/* This file is part of TSRE5. Licensed under GPL-3.0-or-later; see LICENSE.md. */
#ifndef TELEPOLEDATA_H
#define TELEPOLEDATA_H

#include <array>
#include <QString>
#include <QStringList>
#include <QVector>

class TelepoleData {
public:
    struct Config {
        QString fileName;
        QString shadowName;
        float separation = 10.0f;
        QVector<std::array<float, 3>> wires;
        bool valid = false;
    };

    bool load(const QString &path);
    int configCount() const;
    const Config *config(int index) const;
    const QVector<Config> &configs() const;
    const QStringList &diagnostics() const;

    static const TelepoleData &routeData(
            const QString &routePath, bool forceReload = false);

private:
    QVector<Config> configList;
    QStringList loadDiagnostics;
};

#endif
