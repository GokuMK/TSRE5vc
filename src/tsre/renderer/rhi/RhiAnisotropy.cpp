/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "RhiAnisotropy.h"
#include "RhiContext.h"
#include <algorithm>
#include <rhi/qrhi.h>
#if TSRE_RHI_VULKAN
#include <QVulkanFunctions>
#include <QVulkanInstance>
// Qt's private Vulkan sampler: its VkSampler is replaced here. Tied to the
// Qt version TSRE ships with (6.10); check it when Qt is updated.
#include <QtGui/private/qrhivulkan_p.h>
#endif

namespace RhiAnisotropy {

float apply(QRhi *rhi, QRhiSampler *sampler, float level) {
#if TSRE_RHI_VULKAN
    if (rhi == nullptr || sampler == nullptr || level <= 1.0f || rhi->backend() != QRhi::Vulkan)
        return 1.0f;
    if (sampler->minFilter() != QRhiSampler::Linear || sampler->mipmapMode() != QRhiSampler::Linear
            || sampler->textureCompareOp() != QRhiSampler::Never)
        return 1.0f;
    const auto *handles = static_cast<const QRhiVulkanNativeHandles *>(rhi->nativeHandles());
    if (handles == nullptr || handles->inst == nullptr || handles->dev == VK_NULL_HANDLE
            || handles->physDev == VK_NULL_HANDLE)
        return 1.0f;
    QVulkanFunctions *functions = handles->inst->functions();
    VkPhysicalDeviceFeatures features = {};
    functions->vkGetPhysicalDeviceFeatures(handles->physDev, &features);
    if (!features.samplerAnisotropy)
        return 1.0f;
    VkPhysicalDeviceProperties properties = {};
    functions->vkGetPhysicalDeviceProperties(handles->physDev, &properties);
    const float allowed = std::min(level, properties.limits.maxSamplerAnisotropy);
    if (allowed <= 1.0f)
        return 1.0f;
    auto filter = [](QRhiSampler::Filter f) {
        return f == QRhiSampler::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    };
    auto address = [](QRhiSampler::AddressMode mode) {
        return mode == QRhiSampler::ClampToEdge ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                : mode == QRhiSampler::Mirror ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                                              : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };
    // As QVkSampler::create makes it (a mipmapped sampler's maxLod 1000),
    // with anisotropy.
    VkSamplerCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = filter(sampler->magFilter());
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = address(sampler->addressU());
    info.addressModeV = address(sampler->addressV());
    info.addressModeW = address(sampler->addressW());
    info.anisotropyEnable = VK_TRUE;
    info.maxAnisotropy = allowed;
    info.maxLod = 1000.0f;
    QVulkanDeviceFunctions *device = handles->inst->deviceFunctions(handles->dev);
    VkSampler replacement = VK_NULL_HANDLE;
    if (device->vkCreateSampler(handles->dev, &info, nullptr, &replacement) != VK_SUCCESS)
        return 1.0f;
    auto *vulkanSampler = static_cast<QVkSampler *>(sampler);
    if (vulkanSampler->sampler != VK_NULL_HANDLE)
        device->vkDestroySampler(handles->dev, vulkanSampler->sampler, nullptr);
    vulkanSampler->sampler = replacement;
    return allowed;
#else
    Q_UNUSED(rhi);
    Q_UNUSED(sampler);
    Q_UNUSED(level);
    return 1.0f;
#endif
}

}
