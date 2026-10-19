#include "NightTexturesTestSuite.h"
#include "TokenTestSupport.h"
#include <tsre/Game.h>
#include <tsre/shape/NightTextures.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace {

bool touch(const QString &path) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("x") == 1;
}

bool same(const QString &a, const QString &b) {
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
}

}

int TsreTests::runNightTexturesSuite(bool verbose) {
    TokenTest::Suite test{"[tests:night-textures]", verbose};
    QTemporaryDir temp;
    const QString textures = temp.filePath("TEXTURES");
    test.check(touch(textures + "/house.ace") && touch(textures + "/NIGHT/house.ace")
                   && touch(textures + "/SPRING/house.ace") && touch(textures + "/roof.ace")
                   && touch(textures + "/NIGHT/sign.dds") && touch(textures + "/sign.ace"),
               "write the folders");

    test.check(same(NightTextures::find(textures, "house.ace"), textures + "/NIGHT/house.ace"),
               "next to the texture: TEXTURES/NIGHT");
    test.check(same(NightTextures::find(textures + "/SPRING", "house.ace"), textures + "/NIGHT/house.ace"),
               "a seasonal texture: one level up");
    test.check(NightTextures::find(textures, "roof.ace").isEmpty(), "no night texture: none");
    test.check(!NightTextures::find(textures, "sign.ace").isEmpty(), "an .ace name finds the .dds");
    test.check(NightTextures::find(textures, "").isEmpty(), "no name: none");

    // The folder's name in any case (content has NIGHT, Night, night, nIGHT).
    const QString other = temp.filePath("OTHER");
    test.check(touch(other + "/nIGHT/lamp.ace"), "write a folder spelt nIGHT");
    test.check(!NightTextures::find(other, "lamp.ace").isEmpty(), "a Night folder in any case");

    // Loaded below 20 degrees, released above 23.
    test.check(NightTextures::wantedForSun(19.0, false) && !NightTextures::wantedForSun(21.0, false),
               "loaded once the sun is below 20 degrees");
    test.check(NightTextures::wantedForSun(22.0, true) && !NightTextures::wantedForSun(24.0, true),
               "released once it is above 23 again");

    // CMK's textures, when the content is there.
    const QString cmk = Game::root + "/ROUTES/CMK/TEXTURES";
    if (QFileInfo::exists(cmk + "/u79haus0084.ace")) {
        test.check(QFileInfo::exists(NightTextures::find(cmk, "u79haus0084.ace")), "CMK u79haus0084: its night texture");
        test.check(QFileInfo::exists(NightTextures::find(cmk + "/SPRING", "u79haus0084.ace")),
                   "CMK u79haus0084 from SPRING: the same");
    } else if (verbose) {
        qInfo().noquote() << "[tests:night-textures] no CMK textures under" << Game::root;
    }
    return test.finish();
}
