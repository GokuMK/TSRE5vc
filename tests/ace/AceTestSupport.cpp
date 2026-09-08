// Isolate the real ACE/Texture implementation from unrelated application systems.
// No codec, upload, readback, or file I/O implementation is mocked.
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/texture/Brush.h>
int Game::textureQuality = 1;
int Game::AASamples = 0;
bool Game::AARemoveBorder = false;
void Undo::PushTextureData(int, unsigned char *, unsigned int) {}
float Brush::getAlpha(int, int, int) {
    return 1.0f;
}
