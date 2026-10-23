#pragma once

// Whether the 3D view draws the borders of selected objects: the blue boxes
// around world objects (WorldObj::pushRenderBox) and the red frames around
// rolling stock (Eng::pushDrawBorder3d). 3D View > Object Borders; Unselect
// All turns it off with the other overlays.
namespace ObjectBorders {
inline bool shown = true;
}
