#include "ConsistPreviewTestSuite.h"

#include <QApplication>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <shapeViewer/ShapeViewerGLWidget.h>
#include <tsre/camera/CameraConsist.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>

namespace {
// A simple wagon side keeps this widget test independent of installed content.
class WagonShape : public ComplexShape {
public:
    OglObj mesh;
    QString path = "consist-preview-test";
    float bounds[6] = {0, 0, -1, 1, -1, 1};
    const QString &getPathId() const override { return path; }
    bool isLoaded() const override { return true; }
    float getSize() const override { return 2; }
    const float *getBound() const override { return bounds; }
    bool getBoxPoints(QVector<float> &) override { return false; }
    void load() override {}
    void reload() override {}
    unsigned int newState() override { return 0; }
    void setAnimated(unsigned int, bool) override {}
    void updateSim(float, unsigned int) override {}
    void pushRenderItem(RenderQueue &queue) override { pushRenderItem(queue, 0, 0); }
    void pushRenderItem(RenderQueue &queue, quint32 id, unsigned int) override {
        initMesh();
        mesh.pushRenderItem(queue, id);
    }
    void invalidateRenderState(bool) override {}
    void initMesh() {
        if(!mesh.loaded){
            float vertices[] = {0,-1,-1, 0,1,-1, 0,1,1,
                                0,-1,-1, 0,1,1, 0,-1,1};
            mesh.init(vertices, 18, RenderItem::V, GL_TRIANGLES);
            mesh.setMaterial(0.2f, 0.7f, 0.3f);
        }
    }
};
}

int TsreTests::runConsistPreviewSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool ok, const char *name) {
        if(ok){ ++passed; if(verbose) qInfo() << "[tests:consist-preview-gl] PASS" << name; }
        else { ++failed; qWarning() << "[tests:consist-preview-gl] FAIL" << name; }
    };
    EngLib engines;
    QScopedValueRollback<EngLib*> restoreEngines(Game::currentEngLib, &engines);
    QScopedValueRollback<ShapeLib*> restoreShapes(Game::currentShapeLib);
    QScopedValueRollback<decltype(ConLib::con)> restoreConsists(ConLib::con, {});
    // Picking must use the widget's DPR, even if the global route ratio differs.
    QScopedValueRollback<float> restoreRatio(Game::PixelRatio, 3.0f);
    CameraConsist camera(nullptr);
    camera.setPos(10, 0, 0);
    camera.setPlayerRot(-M_PI / 2, 0);
    Consist consist;
    Eng wagon;
    wagon.loaded = 1;
    wagon.sizex = 2; wagon.sizey = 2; wagon.sizez = 2;
    wagon.displayName = "Test wagon";
    engines.eng[0] = &wagon;
    for(int i = 0; i < 2; ++i){
        Consist::EngItem item;
        item.eng = 0;
        item.flip = true;
        item.pos = i == 0 ? -2 : 2;
        consist.engItems.append(item);
    }
    ConLib::con[0] = &consist;
    ShapeViewerGLWidget widget;
    widget.setAttribute(Qt::WA_DontShowOnScreen);
    widget.setCamera(&camera);
    widget.resize(640, 480);
    widget.setBackgroundGlColor(0.1f, 0.1f, 0.1f);
    WagonShape shape;
    widget.currentShapeLib->shape[0] = &shape;
    wagon.shape.id[reinterpret_cast<long long>(widget.currentShapeLib)] = 0;
    widget.showCon(0);
    widget.show();
    QApplication::processEvents();
    const QImage initial = widget.grabFramebuffer();
    check(widget.isValid() && !initial.isNull(), "preview has a working OpenGL framebuffer");
    if(!widget.isValid())
        return 1;
    int selected = -1, selectionSignals = 0;
    QObject::connect(&widget, &ShapeViewerGLWidget::selected, [&](int index) {
        selected = index;
        ++selectionSignals;
    });
    const auto wagonPoint = [&](float z) {
        const float *p = GLUU::get()->pMatrix;
        const float w = p[11] * z + p[15];
        return QPointF((1 + (p[8] * z + p[12]) / w) * widget.width() / 2,
                       (1 - (p[9] * z + p[13]) / w) * widget.height() / 2);
    };
    const auto click = [&](QPointF point, bool moveBeforePaint = false) {
        QMouseEvent press(QEvent::MouseButtonPress, point, point,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &press);
        if(moveBeforePaint){
            QMouseEvent move(QEvent::MouseMove, QPointF(1,1), QPointF(1,1),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&widget, &move);
        }
        const QImage frame = widget.grabFramebuffer();
        QMouseEvent release(QEvent::MouseButtonRelease, point, point,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &release);
        check(!frame.isNull() && GLUU::get()->currentShader == GLUU::get()->shaders["StandardBloom"],
              "pick finishes with the normal preview shader and framebuffer");
    };
    click(wagonPoint(-2));
    check(consist.selectedIdx == 0 && selected == 0 && selectionSignals == 1,
          "first wagon is selectable without a preceding mouse move");
    click(wagonPoint(2), true);
    check(consist.selectedIdx == 1 && selected == 1 && selectionSignals == 2,
          "second wagon uses the press position even if the mouse moves before painting");
    click(QPointF(1,1));
    check(consist.selectedIdx == 1 && selectionSignals == 2,
          "background does not select a wagon or emit a selection");
    widget.resize(800, 400);
    widget.grabFramebuffer();
    click(wagonPoint(-2));
    check(consist.selectedIdx == 0 && selected == 0 && selectionSignals == 3,
          "selection target follows preview resize");
    widget.makeCurrent();
    shape.mesh.deleteVBO();
    widget.currentShapeLib->shape.clear();
    widget.doneCurrent();
    qInfo() << "[tests:consist-preview-gl] passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
