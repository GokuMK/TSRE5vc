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
    const quint32 index = static_cast<quint32>(instanceMatrices.size() / 16);
    instanceMatrices.insert(instanceMatrices.end(), matrix, matrix + 16);
    return index;
}

const float *QueueRenderer::instanceMatrix(quint32 index) const{
    return instanceMatrices.data() + index * 16;
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
    if(packet->material.surface == RenderItem::SURFACE_ALPHA_TEST)
        return PASS_ALPHA_TEST;
    if(packet->material.surface == RenderItem::SURFACE_BLENDED)
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

bool QueueRenderer::instanceBounds(const DrawInstance &instance, float *center,
                                     float &radius) const{
    const RenderItem::Bounds &bounds = instance.packet->bounds;
    if(!bounds.valid())
        return false;
    const float *m = instanceMatrix(instance.matrix);
    for(int i = 0; i < 3; ++i)
        center[i] = m[i] * bounds.center[0] + m[4 + i] * bounds.center[1]
                + m[8 + i] * bounds.center[2] + m[12 + i];
    float scale = 0.0f;
    for(int column = 0; column < 3; ++column){
        const float *axis = m + column * 4;
        scale = std::max(scale, axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    }
    radius = bounds.radius * std::sqrt(scale);
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
    const RenderPass pass = routePass(packet, order);
    DrawInstance instance;
    instance.packet = packet;
    instance.matrix = captureMatrix(matrix);
    instance.selectionId = selectionId != 0 ? selectionId : packet->selectionId;
    instance.order = nextOrder++;
    instance.category = RenderStats::category();
    instance.owned = owned;
    instance.castsShadow = castsShadow(packet);
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

// Orders instances by the first submission of their texture, then of their
// packet, then by submission. The order is stable across runs, unlike
// ordering by pointer or hash.
void QueueRenderer::sortByTexture(std::vector<DrawInstance> &instances){
    if(!groupByTexture || instances.size() < 2)
        return;
    const size_t count = instances.size();
    sortOrder.resize(count);
    for(size_t i = 0; i < count; ++i)
        sortOrder[i] = static_cast<quint32>(i);

    std::sort(sortOrder.begin(), sortOrder.end(), [&instances](quint32 a, quint32 b){
        if(instances[a].packet != instances[b].packet)
            return instances[a].packet < instances[b].packet;
        return instances[a].order < instances[b].order;
    });
    for(size_t i = 0; i < count; ){
        const quint32 first = instances[sortOrder[i]].order;
        size_t j = i;
        for(; j < count && instances[sortOrder[j]].packet == instances[sortOrder[i]].packet; ++j)
            instances[sortOrder[j]].packetRank = first;
        i = j;
    }

    std::sort(sortOrder.begin(), sortOrder.end(), [&instances](quint32 a, quint32 b){
        const quint64 ka = textureKey(instances[a].packet);
        const quint64 kb = textureKey(instances[b].packet);
        if(ka != kb)
            return ka < kb;
        return instances[a].order < instances[b].order;
    });
    for(size_t i = 0; i < count; ){
        const quint32 first = instances[sortOrder[i]].order;
        const quint64 key = textureKey(instances[sortOrder[i]].packet);
        size_t j = i;
        for(; j < count && textureKey(instances[sortOrder[j]].packet) == key; ++j)
            instances[sortOrder[j]].textureRank = first;
        i = j;
    }

    std::sort(instances.begin(), instances.end(), [](const DrawInstance &a, const DrawInstance &b){
        if(a.textureRank != b.textureRank)
            return a.textureRank < b.textureRank;
        if(a.packetRank != b.packetRank)
            return a.packetRank < b.packetRank;
        return a.order < b.order;
    });
}

void QueueRenderer::sortBackToFront(std::vector<DrawInstance> &instances){
    std::sort(instances.begin(), instances.end(), [](const DrawInstance &a, const DrawInstance &b){
        if(a.distance != b.distance)
            return a.distance > b.distance;
        return a.order < b.order;
    });
}

void QueueRenderer::consumePass(PassQueue &queue){
    for(const std::vector<DrawInstance> *list : {&queue.ordered, &queue.grouped})
        for(const DrawInstance &instance : *list)
            if(!instance.owned)
                queuedPackets--;
    queue.ordered.clear();
    queue.grouped.clear();
}

void QueueRenderer::clearQueues(){
    for(PassQueue &queue : passes)
        consumePass(queue);
    for(RenderItem *item : ownedItems)
        delete item;
    ownedItems.clear();
    instanceMatrices.clear();
    nextOrder = 0;
}

void QueueRenderer::resetFrame(){
    clearQueues();
    Renderer::resetFrame();
}

void QueueRenderer::planGroups(const std::vector<DrawInstance> &instances){
    groupPlans.clear();
    instanceUpload.clear();
    instanceVisible.assign(instances.size(), 0);
    for(size_t i = 0; i < instances.size(); ){
        GroupPlan plan;
        plan.begin = i;
        RenderItem *item = instances[i].packet;
        while(i < instances.size() && instances[i].packet == item)
            ++i;
        plan.end = i;
        if(!hasMesh(item)){
            groupPlans.push_back(plan);
            continue;
        }
        bool oneSelection = true;
        quint32 selection = 0;
        for(size_t k = plan.begin; k < plan.end; ++k){
            if(!visible(instances[k], cullFrustum))
                continue;
            instanceVisible[k] = 1;
            if(plan.visible == 0)
                selection = instances[k].selectionId;
            else if(instances[k].selectionId != selection)
                oneSelection = false;
            plan.visible++;
        }
        const int rows = static_cast<int>(instanceUpload.size() / 16);
        if(plan.visible >= 2 && oneSelection && instanceRowsFit(rows, plan.visible)){
            plan.base = rows;
            for(size_t k = plan.begin; k < plan.end; ++k)
                if(instanceVisible[k]){
                    const float *m = instanceMatrix(instances[k].matrix);
                    instanceUpload.insert(instanceUpload.end(), m, m + 16);
                }
        }
        groupPlans.push_back(plan);
    }
}

void QueueRenderer::planShadowCasters(float range, const float *viewProjection){
    const Frustum lightFrustum = frustumOf(viewProjection);
    // Depth does not depend on draw order, so repeated packets draw instanced.
    shadowCasters.clear();
    for(PassQueue &queue : passes){
        for(const std::vector<DrawInstance> *list : {&queue.ordered, &queue.grouped}){
            for(const DrawInstance &instance : *list){
                if(!instance.castsShadow || !hasMesh(instance.packet))
                    continue;
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
                    continue;
                shadowCasters.push_back(&instance);
            }
        }
    }
    std::stable_sort(shadowCasters.begin(), shadowCasters.end(),
                     [](const DrawInstance *a, const DrawInstance *b){
        return std::less<const RenderItem *>()(a->packet, b->packet);
    });
    groupPlans.clear();
    instanceUpload.clear();
    for(size_t i = 0; i < shadowCasters.size(); ){
        GroupPlan plan;
        plan.begin = i;
        while(i < shadowCasters.size() && shadowCasters[i]->packet == shadowCasters[plan.begin]->packet)
            ++i;
        plan.end = i;
        plan.visible = static_cast<int>(plan.end - plan.begin);
        const int rows = static_cast<int>(instanceUpload.size() / 16);
        if(plan.visible >= 2 && instanceRowsFit(rows, plan.visible)){
            plan.base = rows;
            for(size_t k = plan.begin; k < plan.end; ++k){
                const float *m = instanceMatrix(shadowCasters[k]->matrix);
                instanceUpload.insert(instanceUpload.end(), m, m + 16);
            }
        }
        groupPlans.push_back(plan);
    }
}
