/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/renderer/QueueRenderer.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderStats.h>
#include <algorithm>
#include <cmath>

quint64 QueueRenderer::textureKey(const RenderItem *item){
    if(item->material.textureId >= 0)
        return (quint64(1) << 32) | quint32(item->material.textureId);
    return item->material.textureObject;
}

bool QueueRenderer::hasMesh(const RenderItem *item){
    return item->mesh.handle.valid();
}

quint32 QueueRenderer::captureMatrix(const float *matrix){
    // An object's packets come one after another under its matrix: they
    // share one copy (59,000 instances, 6,000 objects at Prudnik's freight
    // station).
    const size_t size = instanceMatrices.size();
    if(size >= 16 && std::memcmp(instanceMatrices.data() + size - 16, matrix, 16 * sizeof(float)) == 0)
        return static_cast<quint32>(size / 16 - 1);
    const quint32 index = static_cast<quint32>(size / 16);
    instanceMatrices.insert(instanceMatrices.end(), matrix, matrix + 16);
    return index;
}

const float *QueueRenderer::instanceMatrix(quint32 index) const{
    return instanceMatrices.data() + index * 16;
}

void QueueRenderer::beginMotionFrame(){
    ++motionFrame;
    motionPrevious.clear();
    // Objects gone (unloaded, out of view) a while: forgotten.
    if((motionFrame & 127) == 0)
        for(auto entry = motionEntries.begin(); entry != motionEntries.end();)
            entry = entry->second.frame + 2 < motionFrame ? motionEntries.erase(entry) : std::next(entry);
}

void QueueRenderer::shiftMotion(float dx, float dz){
    // Last frame's (not yet taken this frame).
    for(auto &entry : motionEntries)
        if(entry.second.frame + 1 == motionFrame)
            for(std::array<float, 16> &matrix : entry.second.now){
                matrix[12] += dx;
                matrix[14] += dz;
            }
}

quint32 QueueRenderer::motionOf(const RenderItem *packet, const float *matrix){
    if(!animated || motionOwner == nullptr || !tracksMotion())
        return 0;
    MotionEntry &entry = motionEntries[MotionKey{motionOwner, motionPart, packet, packet->msMatrix}];
    if(entry.frame != motionFrame){
        if(entry.frame + 1 == motionFrame)
            entry.before.swap(entry.now);
        else
            entry.before.clear();
        entry.now.clear();
        entry.frame = motionFrame;
    }
    const size_t occurrence = entry.now.size();
    entry.now.emplace_back();
    std::array<float, 16> &full = entry.now.back();
    if(packet->msMatrix != nullptr)
        Mat4::multiply(full.data(), const_cast<float *>(matrix), packet->msMatrix);
    else
        std::copy(matrix, matrix + 16, full.begin());
    // New this frame (its shape loaded, it came into view): still, not the
    // static world's motion.
    motionPrevious.push_back(occurrence < entry.before.size() ? entry.before[occurrence] : full);
    return quint32(motionPrevious.size());
}

Renderer::RenderPass QueueRenderer::routePass(const RenderItem *packet,
                                                SubmitOrder order) const{
    switch(currentLayer){
    case LAYER_SKY: return PASS_SKY;
    case LAYER_DISTANT: return PASS_DISTANT;
    case LAYER_OVERLAY: return PASS_OVERLAY;
    case LAYER_WATER: return PASS_WATER;
    case LAYER_UI: return PASS_UI;
    case LAYER_SCENE: break;
    }
    if(packet->material.surface == RenderItem::SURFACE_TERRAIN)
        return PASS_TERRAIN;
    if(packet->pbr.enabled && packet->pbr.transmission > 0.0f)
        return PASS_TRANSMISSION;
    if(order == SUBMIT_ORDERED)
        return PASS_OPAQUE;
    const unsigned char surface = packet->drawSurface();
    if(surface == RenderItem::SURFACE_ALPHA_TEST)
        return PASS_ALPHA_TEST;
    if(surface == RenderItem::SURFACE_BLENDED)
        return PASS_BLENDED;
    return PASS_OPAQUE;
}

bool QueueRenderer::castsShadow(const RenderItem *packet) const{
    if(!shadowCasting)
        return false;
    if(currentLayer != LAYER_SCENE && currentLayer != LAYER_OVERLAY)
        return false;
    if(packet->material.surface == RenderItem::SURFACE_TERRAIN || packet->material.decal)
        return false;
    if(packet->mesh.layout != RenderItem::VNT && packet->mesh.layout != RenderItem::VNTA
            && packet->mesh.layout != RenderItem::PBR)
        return false;
    return packet->mesh.primitive == RenderItem::PRIMITIVE_TRIANGLES;
}

// Position of the packet origin in submission space.
void QueueRenderer::instanceOrigin(const DrawInstance &instance, float *origin) const{
    const float *matrix = instanceMatrix(instance.matrix);
    const float *ms = instance.packet->msMatrix;
    const float local[3] = {ms ? ms[12] : 0.0f, ms ? ms[13] : 0.0f, ms ? ms[14] : 0.0f};
    for(int i = 0; i < 3; ++i)
        origin[i] = matrix[i] * local[0] + matrix[4 + i] * local[1]
                + matrix[8 + i] * local[2] + matrix[12 + i];
}

float QueueRenderer::matrixScale(const float *m){
    float scale = 0.0f;
    for(int column = 0; column < 3; ++column){
        const float *axis = m + column * 4;
        scale = std::max(scale, axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    }
    return std::sqrt(scale);
}

void QueueRenderer::sphereOf(const RenderItem *packet, const float *m, float scale, float *sphere){
    const RenderItem::Bounds &bounds = packet->bounds;
    if(!bounds.valid()){
        sphere[3] = -1.0f;
        return;
    }
    for(int i = 0; i < 3; ++i)
        sphere[i] = m[i] * bounds.center[0] + m[4 + i] * bounds.center[1]
                + m[8 + i] * bounds.center[2] + m[12 + i];
    sphere[3] = bounds.radius * scale;
}

float QueueRenderer::capturedScale(quint32 index){
    // The matrix's scale once for the packets sharing it (captureMatrix).
    if(index != scaleMatrix || instanceMatrices.size() < size_t(index + 1) * 16 || !scaleValid){
        scaleMatrix = index;
        scaleValid = true;
        capturedMatrixScale = matrixScale(instanceMatrix(index));
    }
    return capturedMatrixScale;
}

bool QueueRenderer::instanceBounds(const DrawInstance &instance, float *center,
                                     float &radius) const{
    if(instance.sphere[3] < 0.0f)
        return false;
    std::copy(instance.sphere, instance.sphere + 3, center);
    radius = instance.sphere[3];
    return true;
}

void QueueRenderer::visibleBounds(RenderPass pass, const float *viewProjection,
                                    std::vector<float> &spheres) const{
    spheres.clear();
    const Frustum frustum = frustumOf(viewProjection);
    for(const std::vector<DrawInstance> *list : {&passes[pass].ordered, &passes[pass].grouped})
        for(const DrawInstance &instance : *list){
            float center[3];
            float radius = 0.0f;
            if(!instanceBounds(instance, center, radius) || !intersects(frustum, center, radius))
                continue;
            spheres.insert(spheres.end(), {center[0], center[1], center[2], radius});
        }
}

bool QueueRenderer::visible(const DrawInstance &instance, const Frustum &frustum) const{
    float center[3];
    float radius = 0.0f;
    if(!instanceBounds(instance, center, radius))
        return true;
    bool inside = !frustum.enabled || intersects(frustum, center, radius);
    if(inside && viewLimitsEnabled
            && instance.packet->material.surface != RenderItem::SURFACE_TERRAIN){
        float distance = 0.0f;
        for(int i = 0; i < 3; ++i)
            distance += (center[i] - viewPosition[i]) * (center[i] - viewPosition[i]);
        distance = std::sqrt(distance);
        inside = distance - radius <= viewLimits.maxDistance
                && radius >= viewLimits.minAngularRadius * distance;
    }
    if(inside)
        return true;
    if(RenderStats::inFrame())
        RenderStats::current().culledInstances++;
    return false;
}

void QueueRenderer::queueInstance(RenderItem *packet, const float *matrix,
                                    quint32 selectionId, SubmitOrder order, bool owned){
    if(shadowOnly){
        // A stand-in for the shadow maps only: nothing to draw without a
        // casting mesh.
        if(owned || !hasMesh(packet) || !castsShadow(packet))
            return;
        DrawInstance instance;
        instance.packet = packet;
        instance.matrix = captureMatrix(matrix);
        sphereOf(packet, matrix, capturedScale(instance.matrix), instance.sphere);
        instance.order = nextOrder++;
        instance.category = RenderStats::category();
        instance.castsShadow = true;
        instance.dynamicShadow = shadowDynamic || animated;
        shadowOnlyInstances.push_back(instance);
        queuedPackets++;
        if(RenderStats::inFrame())
            RenderStats::current().groupedInstances++;
        return;
    }
    const RenderPass pass = routePass(packet, order);
    DrawInstance instance;
    instance.packet = packet;
    instance.matrix = captureMatrix(matrix);
    sphereOf(packet, matrix, capturedScale(instance.matrix), instance.sphere);
    instance.selectionId = selectionId != 0 ? selectionId : packet->selectionId;
    instance.order = nextOrder++;
    instance.category = RenderStats::category();
    instance.owned = owned;
    instance.castsShadow = castsShadow(packet);
    // Overlays (trains, markers) and frame items change from frame to frame.
    instance.dynamicShadow = shadowDynamic || animated || owned || currentLayer != LAYER_SCENE;
    instance.animated = animated;
    if(!owned)
        instance.motion = motionOf(packet, instanceMatrix(instance.matrix));
    instance.lightReach = currentLightReach;
    if((pass == PASS_BLENDED || pass == PASS_TRANSMISSION) && order == SUBMIT_GROUPED){
        // Distance from the camera to the packet origin, for back-to-front order.
        float origin[3];
        instanceOrigin(instance, origin);
        float distance = 0.0f;
        for(int i = 0; i < 3; ++i)
            distance += (origin[i] - viewPosition[i]) * (origin[i] - viewPosition[i]);
        instance.distance = distance;
    }
    if(order == SUBMIT_ORDERED)
        passes[pass].ordered.push_back(instance);
    else
        passes[pass].grouped.push_back(instance);
    if(!owned)
        queuedPackets++;
    if(RenderStats::inFrame()){
        if(owned)
            RenderStats::current().queuedItems++;
        else
            RenderStats::current().groupedInstances++;
        RenderStats::current().categories[instance.category].items++;
    }
}

void QueueRenderer::submitFrameItem(RenderItem* r){
    if(r == NULL)
        return;
    RenderItem *queuedItem = r;
    if(r->shared){
        queuedItem = new RenderItem(*r);
        queuedItem->shared = false;
    }
    ownedItems.push_back(queuedItem);
    queueInstance(queuedItem, mvMatrix, queuedItem->selectionId, SUBMIT_ORDERED, true);
}

void QueueRenderer::submit(RenderItem *packet, quint32 selectionId, SubmitOrder order){
    if(packet != NULL)
        queueInstance(packet, mvMatrix, selectionId, order, false);
}

void QueueRenderer::submit(const QVector<RenderItem*> &items, quint32 selectionId){
    for(RenderItem *packet : items){
        if(packet != NULL)
            queueInstance(packet, mvMatrix, selectionId, SUBMIT_GROUPED, false);
    }
}

namespace {

// An address's entry in a table of 2^(64 - shift) entries (Fibonacci
// hashing: the product's top bits).
size_t slotIndex(const void *address, int shift){
    return size_t((quint64(quintptr(address)) * 0x9E3779B97F4A7C15ull) >> shift);
}

}

quint32 QueueRenderer::packetSlotOf(const RenderItem *packet){
    if(slotPackets.size() * 2 >= packetSlots.size()){
        // Half full: twice the entries, this walk's packets in them again.
        const size_t size = std::max<size_t>(1024, packetSlots.size() * 2);
        packetSlots.assign(size, PacketSlot());
        packetSlotShift = 64;
        for(size_t rest = size; rest > 1; rest >>= 1)
            packetSlotShift--;
        packetSlotStamp = 1;
        for(size_t slot = 0; slot < slotPackets.size(); ++slot){
            size_t i = slotIndex(slotPackets[slot], packetSlotShift);
            while(packetSlots[i].stamp == packetSlotStamp)
                i = (i + 1) & (size - 1);
            packetSlots[i] = PacketSlot{slotPackets[slot], packetSlotStamp, quint32(slot)};
        }
    }
    const size_t mask = packetSlots.size() - 1;
    for(size_t i = slotIndex(packet, packetSlotShift);; i = (i + 1) & mask){
        PacketSlot &entry = packetSlots[i];
        if(entry.stamp != packetSlotStamp){
            entry = PacketSlot{packet, packetSlotStamp, quint32(slotPackets.size())};
            slotPackets.push_back(packet);
            slotCounts.push_back(0);
            return entry.slot;
        }
        if(entry.packet == packet)
            return entry.slot;
    }
}

// The order is stable across runs, unlike ordering by pointer or hash.
void QueueRenderer::orderByTexture(const std::vector<DrawInstance> &instances, std::vector<quint32> &order){
    const size_t count = instances.size();
    order.resize(count);
    if(!groupByTexture || count < 2){
        for(size_t i = 0; i < count; ++i)
            order[i] = static_cast<quint32>(i);
        return;
    }
    // The packets by their first submission, a slot each.
    if(++packetSlotStamp == 0){
        std::fill(packetSlots.begin(), packetSlots.end(), PacketSlot());
        packetSlotStamp = 1;
    }
    slotPackets.clear();
    slotCounts.clear();
    instanceSlots.resize(count);
    for(size_t i = 0; i < count; ++i){
        const quint32 slot = packetSlotOf(instances[i].packet);
        instanceSlots[i] = slot;
        slotCounts[slot]++;
    }
    // Their textures by their first packet: the slots by texture, then each
    // texture's run of them by its first slot.
    const size_t packets = slotPackets.size();
    slotTextures.resize(packets);
    for(size_t slot = 0; slot < packets; ++slot)
        slotTextures[slot] = {textureKey(slotPackets[slot]), static_cast<quint32>(slot)};
    std::sort(slotTextures.begin(), slotTextures.end());
    textureRuns.clear();
    for(size_t i = 0; i < packets; ){
        size_t j = i + 1;
        while(j < packets && slotTextures[j].first == slotTextures[i].first)
            ++j;
        textureRuns.push_back({slotTextures[i].second, static_cast<quint32>(i), static_cast<quint32>(j)});
        i = j;
    }
    std::sort(textureRuns.begin(), textureRuns.end());
    // Each slot's first place, its instances from there in submission order.
    quint32 place = 0;
    for(const std::array<quint32, 3> &run : textureRuns)
        for(quint32 k = run[1]; k < run[2]; ++k){
            quint32 &counted = slotCounts[slotTextures[k].second];
            const quint32 instancesOfSlot = counted;
            counted = place;
            place += instancesOfSlot;
        }
    for(size_t i = 0; i < count; ++i)
        order[slotCounts[instanceSlots[i]]++] = static_cast<quint32>(i);
}

bool QueueRenderer::opaqueTexelsIn(int pass, bool enabled){
    bool take = false;
    if(pass == PASS_ALPHA_TEST){
        take = enabled;
        opaqueTexelsDrawn = take;
    } else if(pass == PASS_BLENDED){
        take = enabled && !opaqueTexelsDrawn;
        opaqueTexelsDrawn = false;
    }
    twoPartBlended.clear();
    if(!take)
        return false;
    for(const DrawInstance &instance : passes[PASS_BLENDED].grouped)
        if(instance.packet->blendsInTwoParts())
            twoPartBlended.push_back(instance);
    orderByTexture(twoPartBlended, twoPartOrder);
    return !twoPartBlended.empty();
}

bool QueueRenderer::blendedInTwo(){
    static const bool enabled = qEnvironmentVariable("TSRE_BLENDED_IN_TWO", "1") != "0";
    return enabled;
}

int QueueRenderer::texelsOf(const RenderItem *packet) const{
    return blendedTexels != TEXELS_ALL && packet->blendsInTwoParts() ? blendedTexels : TEXELS_ALL;
}

void QueueRenderer::orderBackToFront(const std::vector<DrawInstance> &instances, std::vector<quint32> &order){
    const size_t count = instances.size();
    distanceOrder.resize(count);
    for(size_t i = 0; i < count; ++i)
        distanceOrder[i] = {-instances[i].distance, static_cast<quint32>(i)};
    std::sort(distanceOrder.begin(), distanceOrder.end());
    order.resize(count);
    for(size_t i = 0; i < count; ++i)
        order[i] = distanceOrder[i].second;
}

const std::vector<quint32> &QueueRenderer::groupedOrder(int pass){
    PassQueue &queue = passes[pass];
    if(queue.orderSize != queue.grouped.size() || queue.orderByTexture != groupByTexture){
        if(pass == PASS_BLENDED || pass == PASS_TRANSMISSION)
            orderBackToFront(queue.grouped, queue.order);
        else
            orderByTexture(queue.grouped, queue.order);
        queue.orderSize = queue.grouped.size();
        queue.orderByTexture = groupByTexture;
    }
    return queue.order;
}

void QueueRenderer::consumePass(PassQueue &queue){
    for(const std::vector<DrawInstance> *list : {&queue.ordered, &queue.grouped})
        for(const DrawInstance &instance : *list)
            if(!instance.owned)
                queuedPackets--;
    queue.ordered.clear();
    queue.grouped.clear();
    queue.orderSize = size_t(-1);
}

void QueueRenderer::clearQueues(){
    for(PassQueue &queue : passes)
        consumePass(queue);
    queuedPackets -= static_cast<int>(shadowOnlyInstances.size());
    shadowOnlyInstances.clear();
    for(RenderItem *item : ownedItems)
        delete item;
    ownedItems.clear();
    instanceMatrices.clear();
    scaleValid = false;
    nextOrder = 0;
}

void QueueRenderer::resetFrame(){
    clearQueues();
    Renderer::resetFrame();
}

void QueueRenderer::planGroups(const std::vector<DrawInstance> &instances, const std::vector<quint32> &order){
    groupPlans.clear();
    instanceUpload.clear();
    plannedInstances.clear();
    int rows = 0;
    for(size_t i = 0; i < order.size(); ){
        const size_t run = i;
        RenderItem *item = instances[order[i]].packet;
        while(i < order.size() && instances[order[i]].packet == item)
            ++i;
        if(!hasMesh(item))
            continue;
        GroupPlan plan;
        plan.begin = plannedInstances.size();
        bool oneSelection = true;
        quint32 selection = 0;
        for(size_t k = run; k < i; ++k){
            const DrawInstance &instance = instances[order[k]];
            if(!visible(instance, cullFrustum))
                continue;
            if(plannedInstances.size() == plan.begin)
                selection = instance.selectionId;
            else if(instance.selectionId != selection)
                oneSelection = false;
            plannedInstances.push_back(&instance);
        }
        plan.end = plannedInstances.size();
        plan.visible = static_cast<int>(plan.end - plan.begin);
        if(plan.visible == 0)
            continue;
        if(plan.visible >= 2 && oneSelection && instanceRowsFit(rows, plan.visible)){
            plan.base = rows;
            rows += plan.visible;
            if(instanceRowsCopied)
                for(size_t k = plan.begin; k < plan.end; ++k){
                    const float *m = instanceMatrix(plannedInstances[k]->matrix);
                    instanceUpload.insert(instanceUpload.end(), m, m + 16);
                }
        }
        groupPlans.push_back(plan);
    }
}

void QueueRenderer::gatherLights(std::vector<LightGrid::Light> &lights, float exposure,
                                 float emissiveGain, bool emittersOnly, float signalGain, float rangeGain) const{
    lights.clear();
    for(const PassQueue &queue : passes){
        for(const std::vector<DrawInstance> *list : {&queue.ordered, &queue.grouped}){
            for(const DrawInstance &instance : *list){
                const RenderItem *packet = instance.packet;
                if(packet == nullptr || packet->lights.isEmpty())
                    continue;
                // Model space to submission space.
                float transform[16];
                const float *matrix = instanceMatrix(instance.matrix);
                if(packet->msMatrix != nullptr)
                    Mat4::multiply(transform, const_cast<float *>(matrix), packet->msMatrix);
                else
                    std::copy(matrix, matrix + 16, transform);
                for(const RenderItem::Light &source : packet->lights){
                    if(emittersOnly && !source.emissive)
                        continue;
                    if(source.signal && !emittersOnly)
                        continue;
                    LightGrid::Light light;
                    const float *p = source.position;
                    const float *d = source.direction;
                    float length = 0.0f;
                    for(int row = 0; row < 3; ++row){
                        light.position[row] = transform[row] * p[0] + transform[4 + row] * p[1]
                                + transform[8 + row] * p[2] + transform[12 + row];
                        light.direction[row] = transform[row] * d[0] + transform[4 + row] * d[1]
                                + transform[8 + row] * d[2];
                        length += light.direction[row] * light.direction[row];
                    }
                    length = std::sqrt(length);
                    for(float &component : light.direction)
                        component = length > 0.0f ? component / length : 0.0f;
                    // Scaled shapes scale their emitters.
                    const float scale = std::sqrt(transform[0] * transform[0] + transform[1] * transform[1]
                                                  + transform[2] * transform[2]);
                    const float gain = !source.emissive ? exposure
                            : (source.signal ? signalGain : emissiveGain) * scale * scale;
                    for(int c = 0; c < 3; ++c)
                        light.color[c] = source.color[c] * source.intensity * gain;
                    light.radius = source.radius * scale;
                    light.signal = source.signal;
                    const float brightness = std::max({light.color[0], light.color[1], light.color[2]}) * rangeGain;
                    // Up to the default reach, or the reach its object asks
                    // for; the asset's own range can only shorten it.
                    const float reach = LightGrid::rangeFor(brightness, light.radius,
                            instance.lightReach > 0.0f ? std::min(instance.lightReach, LightGrid::MaxReach)
                                                       : LightGrid::DefaultReach);
                    light.range = source.range > 0.0f ? std::min(source.range * scale, reach) : reach;
                    light.spot = source.type == RenderItem::Light::SPOT;
                    light.cosInner = std::cos(source.innerCone);
                    light.cosOuter = std::cos(source.outerCone);
                    if(light.range > 0.0f || emittersOnly)
                        lights.push_back(light);
                }
            }
        }
    }
}

void QueueRenderer::planShadowCasters(float range, const float *viewProjection, bool enclosure,
                                      float minRadius, ShadowCasters casters, ShadowSet set){
    const Frustum lightFrustum = frustumOf(viewProjection);
    // Static casters still loading in reach, by their place (a shape's size
    // is not known before it loads): what a cached map misses.
    constexpr float WaitingReach = 100.0f;
    plannedShapesWaiting = 0;
    if(!enclosure && casters != CASTERS_DYNAMIC)
        for(const std::array<float, 3> &place : waitingShapes){
            const float dx = place[0] - viewPosition[0];
            const float dz = place[2] - viewPosition[2];
            const float reach = range + WaitingReach;
            if(dx * dx + dz * dz <= reach * reach)
                ++plannedShapesWaiting;
        }
    // Depth does not depend on draw order, so repeated packets draw instanced.
    shadowCasters.clear();
    const auto consider = [&](const DrawInstance &instance, bool terrain){
        if((!enclosure && !instance.castsShadow) || !hasMesh(instance.packet))
            return;
        // A cached map's static casters, or the moving ones on top (task 37).
        if((casters == CASTERS_STATIC && instance.dynamicShadow)
                || (casters == CASTERS_DYNAMIC && !instance.dynamicShadow))
            return;
        // The main maps or vegetation's own.
        if((set == SET_MAIN && instance.packet->material.vegetation)
                || (set == SET_VEGETATION && !instance.packet->material.vegetation))
            return;
        // Vegetation is never a roof (task 37), nor is what animates.
        if(enclosure && (instance.packet->material.vegetation || instance.animated))
            return;
        // Measure the range to the bounds, so long meshes whose origin
        // is out of range still cast near the camera.
        float center[3];
        float radius = 0.0f;
        if(!instanceBounds(instance, center, radius))
            instanceOrigin(instance, center);
        const float dx = center[0] - viewPosition[0];
        const float dz = center[2] - viewPosition[2];
        const float reach = range + radius;
        if(dx * dx + dz * dz > reach * reach || !visible(instance, lightFrustum))
            return;
        // The far enclosure map: terrain and large objects only.
        if(!terrain && radius < minRadius)
            return;
        shadowCasters.push_back(&instance);
    };
    for(int pass = 0; pass < PASS_COUNT; ++pass){
        // The enclosure maps: opaque, alpha-tested and blended surfaces, as
        // the shadow maps take them (thin roofs: CMK's hall); vegetation has
        // its own flag and stays out (above). Alpha-tested and blended roofs
        // were kept out for tree canopies before that flag (the user,
        // 2026-10-28).
        if(enclosure && pass != PASS_TERRAIN && pass != PASS_OPAQUE && pass != PASS_ALPHA_TEST
                && pass != PASS_BLENDED)
            continue;
        PassQueue &queue = passes[pass];
        for(const std::vector<DrawInstance> *list : {&queue.ordered, &queue.grouped})
            for(const DrawInstance &instance : *list)
                consider(instance, pass == PASS_TERRAIN);
    }
    // Stand-ins for the shadow maps (task 37); the enclosure takes the view's
    // meshes.
    if(!enclosure)
        for(const DrawInstance &instance : shadowOnlyInstances)
            consider(instance, false);
    std::stable_sort(shadowCasters.begin(), shadowCasters.end(),
                     [](const DrawInstance *a, const DrawInstance *b){
        return std::less<const RenderItem *>()(a->packet, b->packet);
    });
    groupPlans.clear();
    instanceUpload.clear();
    int rows = 0;
    for(size_t i = 0; i < shadowCasters.size(); ){
        GroupPlan plan;
        plan.begin = i;
        while(i < shadowCasters.size() && shadowCasters[i]->packet == shadowCasters[plan.begin]->packet)
            ++i;
        plan.end = i;
        plan.visible = static_cast<int>(plan.end - plan.begin);
        if(plan.visible >= 2 && instanceRowsFit(rows, plan.visible)){
            plan.base = rows;
            rows += plan.visible;
            if(instanceRowsCopied)
                for(size_t k = plan.begin; k < plan.end; ++k){
                    const float *m = instanceMatrix(shadowCasters[k]->matrix);
                    instanceUpload.insert(instanceUpload.end(), m, m + 16);
                }
        }
        groupPlans.push_back(plan);
    }
}
