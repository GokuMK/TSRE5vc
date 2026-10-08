// Isolate the real ACE/Texture implementation from unrelated application systems.
// No codec, upload, readback, or file I/O implementation is mocked.
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/texture/Brush.h>
#include <tsre/renderer/rhi/RhiTextures.h>
int Game::textureQuality = 1;
// The tool uploads with OpenGL only.
QString Game::renderBackend = "opengl";
namespace RhiTextures {
unsigned int create(int, int, const QVector<QByteArray> &) { return 0; }
bool supportsBlocks() { return false; }
unsigned int createCompressed(int, int, Blocks, const QVector<QByteArray> &) { return 0; }
void release(unsigned int) {}
void setSampling(unsigned int, bool, bool) {}
bool clampedToEdge(unsigned int) { return false; }
}
int Game::AASamples = 0;
bool Game::AARemoveBorder = false;
void Undo::PushTextureData(int, unsigned char *, unsigned int) {}
float Brush::getAlpha(int, int, int) {
    return 1.0f;
}
