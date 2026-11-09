/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "RhiPassTimes.h"
#include "RhiContext.h"
#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>
#include <map>
#include <rhi/qrhi.h>
#include <string>
#include <vector>
#if TSRE_RHI_VULKAN
#include <QVulkanFunctions>
#include <QVulkanInstance>
#endif

namespace {
constexpr int Slots = 4;
// Timestamps a frame: two a flushed target.
constexpr int Queries = 512;
}

struct RhiPassTimes::Data {
    QRhi *rhi = nullptr;
#if TSRE_RHI_VULKAN
    VkDevice device = VK_NULL_HANDLE;
    QVulkanDeviceFunctions *f = nullptr;
    VkQueryPool pools[Slots] = {};
#endif
    double nanosecondsPerTick = 1.0;
    // The targets of a slot's frame, a pair of timestamps each; whether its
    // pool was reset in the frame; the next timestamp, -1 when none is open.
    std::vector<std::pair<int, std::string>> recorded[Slots];
    bool reset[Slots] = {};
    int open = -1;
    int slot = 0;
    // Per target (its name): its time in each frame read so far.
    std::map<std::string, std::vector<double>> frames;
    int framesRead = 0;
    QElapsedTimer logged;
};

bool RhiPassTimes::enabled() {
    static const bool on = qEnvironmentVariableIntValue("TSRE_GPU_PASS_TIMES") == 1;
    return on;
}

RhiPassTimes::RhiPassTimes(QRhi *rhi) : d(new Data) {
    d->rhi = rhi;
#if TSRE_RHI_VULKAN
    if (rhi == nullptr || rhi->backend() != QRhi::Vulkan) {
        qInfo() << "rhi-pass-times: Vulkan only";
        return;
    }
    const auto *handles = static_cast<const QRhiVulkanNativeHandles *>(rhi->nativeHandles());
    if (handles == nullptr || handles->inst == nullptr || handles->dev == VK_NULL_HANDLE)
        return;
    VkPhysicalDeviceProperties properties = {};
    handles->inst->functions()->vkGetPhysicalDeviceProperties(handles->physDev, &properties);
    d->nanosecondsPerTick = properties.limits.timestampPeriod;
    d->device = handles->dev;
    d->f = handles->inst->deviceFunctions(handles->dev);
    VkQueryPoolCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = Queries;
    for (VkQueryPool &pool : d->pools)
        if (d->f->vkCreateQueryPool(d->device, &info, nullptr, &pool) != VK_SUCCESS)
            pool = VK_NULL_HANDLE;
    d->logged.start();
#endif
}

RhiPassTimes::~RhiPassTimes() {
#if TSRE_RHI_VULKAN
    if (d->f != nullptr) {
        d->rhi->finish();
        for (VkQueryPool pool : d->pools)
            if (pool != VK_NULL_HANDLE)
                d->f->vkDestroyQueryPool(d->device, pool, nullptr);
    }
#endif
}

void RhiPassTimes::frame() {
#if TSRE_RHI_VULKAN
    if (d->f == nullptr)
        return;
    d->slot = std::clamp(d->rhi->currentFrameSlot(), 0, Slots - 1);
    d->open = -1;
    std::vector<std::pair<int, std::string>> &recorded = d->recorded[d->slot];
    const VkQueryPool pool = d->pools[d->slot];
    if (!recorded.empty() && pool != VK_NULL_HANDLE) {
        std::vector<quint64> ticks(recorded.size() * 2);
        if (d->f->vkGetQueryPoolResults(d->device, pool, 0, quint32(ticks.size()), ticks.size() * sizeof(quint64),
                                        ticks.data(), sizeof(quint64), VK_QUERY_RESULT_64_BIT)
                == VK_SUCCESS) {
            std::map<std::string, double> sums;
            double whole = 0.0;
            for (size_t k = 0; k < recorded.size(); ++k) {
                const double ms = double(ticks[2 * k + 1] - ticks[2 * k]) * d->nanosecondsPerTick / 1e6;
                sums[recorded[k].second] += ms;
                whole += ms;
            }
            sums["(all timed)"] = whole;
            // From the first pass's start to the last's end: the timed
            // passes and the gaps between them (barriers, untimed work).
            const double span = double(ticks[ticks.size() - 1] - ticks[0]) * d->nanosecondsPerTick / 1e6;
            sums["(span)"] = span;
            sums["(between)"] = span - whole;
            for (const auto &[name, ms] : sums)
                d->frames[name].push_back(ms);
            ++d->framesRead;
        }
    }
    recorded.clear();
    d->reset[d->slot] = false;
    if (d->logged.elapsed() >= 2000 && d->framesRead > 0) {
        d->logged.restart();
        std::vector<std::pair<double, std::string>> lines;
        for (auto &[name, values] : d->frames) {
            // Frames without it count as 0.
            values.resize(std::max(values.size(), size_t(d->framesRead)), 0.0);
            std::sort(values.begin(), values.end());
            lines.push_back({values[values.size() / 2], name});
        }
        std::sort(lines.begin(), lines.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        QString text;
        for (const auto &[ms, name] : lines)
            text += QString(" | %1 %2").arg(QString::fromStdString(name)).arg(ms, 0, 'f', 3);
        qInfo().noquote() << "rhi-pass-times: medians of" << d->framesRead << "frames (ms)" << text;
        d->frames.clear();
        d->framesRead = 0;
    }
#endif
}

void RhiPassTimes::begin(QRhiCommandBuffer *cb) {
#if TSRE_RHI_VULKAN
    const VkQueryPool pool = d->pools[d->slot];
    if (d->f == nullptr || pool == VK_NULL_HANDLE || cb == nullptr)
        return;
    const int index = int(d->recorded[d->slot].size()) * 2;
    if (index + 2 > Queries)
        return;
    cb->beginExternal();
    const auto *native = static_cast<const QRhiVulkanCommandBufferNativeHandles *>(cb->nativeHandles());
    if (native != nullptr && native->commandBuffer != VK_NULL_HANDLE) {
        if (!d->reset[d->slot]) {
            d->f->vkCmdResetQueryPool(native->commandBuffer, pool, 0, Queries);
            d->reset[d->slot] = true;
        }
        d->f->vkCmdWriteTimestamp(native->commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, quint32(index));
        d->open = index;
    }
    cb->endExternal();
#else
    (void)cb;
#endif
}

void RhiPassTimes::end(QRhiCommandBuffer *cb, int target, const char *name) {
#if TSRE_RHI_VULKAN
    const VkQueryPool pool = d->pools[d->slot];
    if (d->f == nullptr || pool == VK_NULL_HANDLE || cb == nullptr || d->open < 0)
        return;
    cb->beginExternal();
    const auto *native = static_cast<const QRhiVulkanCommandBufferNativeHandles *>(cb->nativeHandles());
    if (native != nullptr && native->commandBuffer != VK_NULL_HANDLE) {
        d->f->vkCmdWriteTimestamp(native->commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool,
                                  quint32(d->open + 1));
        d->recorded[d->slot].push_back({target, name != nullptr ? std::string(name) : std::to_string(target)});
    }
    d->open = -1;
    cb->endExternal();
#else
    (void)cb;
    (void)target;
    (void)name;
#endif
}
