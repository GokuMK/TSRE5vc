#pragma once
#include <QString>
#include <functional>
class QWidget;
namespace TerrainBakeCommand {
int run(int argc, char **argv);
// Runs only in the isolated command process (no editor/global-state mutation).
bool bakeRoute(const QString &route, const QString &season, int resolution,
               const std::function<void(const QString &)> &progress, QString &error);
void showDialog(QWidget *parent, const QString &route);
}
