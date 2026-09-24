#include <tsre/tests/QuadTreeRecoveryTestSuite.h>
#include <tsre/Game.h>
#include <QCoreApplication>
#include <mzip/miniz/miniz.h>
QString Game::root;
QString Game::route;
bool Game::writeEnabled = true;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const int scan = app.arguments().indexOf("--scan");
    if (scan >= 0 && scan + 1 >= app.arguments().size()) return 2;
    return TsreTests::runQuadTreeRecoverySuite(app.arguments().contains("--verbose"),
        scan >= 0 ? app.arguments()[scan + 1] : QString());
}
