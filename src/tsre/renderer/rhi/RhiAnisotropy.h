/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RHIANISOTROPY_H
#define RHIANISOTROPY_H

class QRhi;
class QRhiSampler;

// Anisotropic filtering for QRhi samplers, which have no setting for it (Qt
// 6.10): mipmapped textures on surfaces seen at a slant (track, ground,
// platforms) blurred, and a driver's forced anisotropy does not reach
// Vulkan samplers (an NVIDIA user, 2026-10-29).
namespace RhiAnisotropy {

// On Vulkan, makes the sampler's VkSampler again as Qt made it, with
// anisotropy up to level (and the device's limit). Only trilinear samplers
// without comparison; call it right after the sampler's create(), before
// any use. Returns the level the sampler took: 1 for none (other APIs, no
// device support, level 1 or less).
float apply(QRhi *rhi, QRhiSampler *sampler, float level);

}

#endif
