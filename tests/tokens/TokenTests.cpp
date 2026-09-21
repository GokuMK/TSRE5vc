#include <tsre/tests/TokenIdTestSuite.h>
#include <QCoreApplication>
#include <mzip/miniz/miniz.h>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    return TsreTests::runTokenIdSuite(app.arguments().contains("--verbose"));
}
