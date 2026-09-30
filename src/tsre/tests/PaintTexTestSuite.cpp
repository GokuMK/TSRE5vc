#include "PaintTexTestSuite.h"
#include "TokenTestSupport.h"
#include <tsre/texture/PaintTexLib.h>
#include <QColor>

namespace {
bool hasOpaqueColor(const Texture &texture, const QColor &color) {
    for(int i = 0; i + 3 < texture.imageSize; i += 4){
        const unsigned char *pixel = texture.imageData + i;
        if(pixel[3] == 255 && pixel[0] == color.red()
                && pixel[1] == color.green() && pixel[2] == color.blue())
            return true;
    }
    return false;
}
}

int TsreTests::runPaintTexSuite(bool verbose) {
    TokenTest::Suite test{"[tests:paint-text]", verbose};
    // Exercise the generated RGBA pixels, including colors used by consist
    // names/numbers and engine-type markers, without requiring an OpenGL driver.
    for(const QString &name : {QString("#ffff00"), QString("#ff0000"),
                              QString("#ffffff"), QString("#000000"),
                              QString("blue")}){
        Texture texture("Wagon.size:8.color:" + name + ".:paintTex");
        PaintTexLib painter;
        painter.texture = &texture;
        painter.run();
        test.check(texture.loaded && hasOpaqueColor(texture, QColor::fromString(name)),
                   "text pixels retain requested color " + name);
        test.check(texture.imageData[3] == 0, "text background stays transparent");
        delete[] texture.imageData;
        texture.imageData = nullptr;
    }
    Texture outlined("Wagon.resm:2.size:8.color:#ff0000.ocolor:#00ff00.:paintTex");
    PaintTexLib painter;
    painter.texture = &outlined;
    painter.run();
    test.check(hasOpaqueColor(outlined, QColor(255, 0, 0)),
               "outlined text retains its fill color");
    test.check(hasOpaqueColor(outlined, QColor(0, 255, 0)),
               "outline retains its independent color");
    delete[] outlined.imageData;
    outlined.imageData = nullptr;
    return test.finish();
}
