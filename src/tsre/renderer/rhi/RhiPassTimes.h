/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RHIPASSTIMES_H
#define RHIPASSTIMES_H

#include <memory>

class QRhi;
class QRhiCommandBuffer;

// The GPU time of the renderer's passes in real frames (TSRE_GPU_PASS_TIMES=1,
// Vulkan only): a timestamp before and after each flushed target's commands
// (its uploads and pass), summed a frame by target and logged every 2 s as
// medians ("rhi-pass-times"), with the span from the first to the last and
// what of it lies between them (barriers, work outside the targets).
// RenderDoc's replay times every event alone, at other clocks (on the Steam
// Deck the same passes took 40 % longer in one capture than in another);
// this measures the frames as they run. Off, nothing is made.
class RhiPassTimes {
public:
    static bool enabled();
    explicit RhiPassTimes(QRhi *rhi);
    ~RhiPassTimes();
    // A new frame: the results of the frame its slot held last (complete
    // by now) taken, the log written when due.
    void frame();
    void begin(QRhiCommandBuffer *cb);
    // target: the renderer's own number for it; name, how the log calls it.
    void end(QRhiCommandBuffer *cb, int target, const char *name);

private:
    struct Data;
    std::unique_ptr<Data> d;
};

#endif
