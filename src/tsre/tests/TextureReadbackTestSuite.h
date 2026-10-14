#ifndef TSRE_TEXTURE_READBACK_TEST_SUITE_H
#define TSRE_TEXTURE_READBACK_TEST_SUITE_H

namespace TsreTests {
// Textures on the QRhi renderer give their pixels back after the upload
// freed them (RhiTextures::readBack): Texture::setEditable and texture
// copies, for RGB, RGBA and block-compressed textures. What Make from
// Overlay and terrain painting need.
int runTextureReadbackSuite(bool verbose);
}

#endif
