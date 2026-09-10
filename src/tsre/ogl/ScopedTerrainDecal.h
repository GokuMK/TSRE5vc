#pragma once
#include <QOpenGLFunctions>

// Ground-only transfers bias the depth test, but leave terrain depth intact.
// Restore caller state, including a previously enabled polygon offset.
class ScopedTerrainDecal {
public:
    ScopedTerrainDecal(QOpenGLFunctions *functions, bool enabled)
        : f(functions), active(enabled) {
        if (!active) return;
        f->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        offsetEnabled = f->glIsEnabled(GL_POLYGON_OFFSET_FILL);
        f->glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &factor);
        f->glGetFloatv(GL_POLYGON_OFFSET_UNITS, &units);
        f->glDepthMask(GL_FALSE);
        f->glEnable(GL_POLYGON_OFFSET_FILL);
        f->glPolygonOffset(-2.0f, -2.0f);
    }
    ~ScopedTerrainDecal() {
        if (!active) return;
        f->glPolygonOffset(factor, units);
        if (!offsetEnabled) f->glDisable(GL_POLYGON_OFFSET_FILL);
        f->glDepthMask(depthMask);
    }
    ScopedTerrainDecal(const ScopedTerrainDecal&) = delete;
    ScopedTerrainDecal& operator=(const ScopedTerrainDecal&) = delete;
private:
    QOpenGLFunctions *f;
    bool active;
    GLboolean depthMask = GL_TRUE, offsetEnabled = GL_FALSE;
    GLfloat factor = 0, units = 0;
};
