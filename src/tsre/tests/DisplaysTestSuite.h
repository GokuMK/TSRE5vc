#pragma once

namespace TsreTests {
// Displays (task scripting 02): a document drawn into a texture by the
// OpenGL renderer (offscreen context) and by the QRhi UI drawer into a
// render-target texture (the shared QRhi: Vulkan under a display, else
// OpenGL ES): its top row the texture's first, as an image's; a data
// binding's change drawn again into the same texture; the mipmaps made.
// The .sd's blocks (from the file and from the complex backend's parsed
// .sd); a route's displays: the nearest platform line, the departures and
// arrivals from the trains' plans and states, a frame's redraws. The
// overrides' packets: the shape-materials suite.
int runDisplaysSuite(bool verbose);
}
