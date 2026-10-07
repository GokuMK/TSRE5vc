# Runs inside qrenderdoc (Inspect-Capture.ps1 -Script draws): finds draws by
# filter and reports what happens to them: state, inputs, vertex shader
# output on screen, pixel history, and the colour target after them.
#
# Filters (all optional, combined): RDC_STRIDE (byte stride of vertex
# buffer 0; 8 is paged terrain), RDC_MIN_WIDTH (viewport width, to skip
# shadow and environment passes), RDC_EVENT (one event id). RDC_DUMP: how
# many draws to report in detail (3). RDC_PIXEL_HISTORY=1: pixel history at
# an on-screen vertex of the first draw that has one.
import os, struct, traceback
import renderdoc as rd

capture = os.environ['RDC_CAPTURE']
report = os.environ['RDC_REPORT']
log = open(report, 'w')
def p(*a):
    log.write(' '.join(str(x) for x in a) + '\n'); log.flush()

def env_int(name, default=None):
    v = os.environ.get(name)
    return int(v) if v else default

def walk(actions, out):
    for a in actions:
        if a.flags & rd.ActionFlags.Drawcall:
            out.append(a)
        walk(a.children, out)

def vs_positions(ctl):
    # Clip-space positions of the draw's vertices (the first VS output).
    mesh = ctl.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
    if mesh.vertexResourceId == rd.ResourceId.Null():
        return []
    count = mesh.numIndices
    if mesh.indexResourceId != rd.ResourceId.Null():
        data = ctl.GetBufferData(mesh.indexResourceId, mesh.indexByteOffset, mesh.numIndices * mesh.indexByteStride)
        fmt = 'H' if mesh.indexByteStride == 2 else 'I'
        count = max(struct.unpack('<%d%s' % (mesh.numIndices, fmt), data)) + 1
    data = ctl.GetBufferData(mesh.vertexResourceId, mesh.vertexByteOffset, count * mesh.vertexByteStride)
    return [struct.unpack_from('<4f', data, k * mesh.vertexByteStride) for k in range(count)]

try:
    stride = env_int('RDC_STRIDE')
    min_width = env_int('RDC_MIN_WIDTH')
    event = env_int('RDC_EVENT')
    dump = env_int('RDC_DUMP', 3)
    cap = rd.OpenCaptureFile()
    cap.OpenFile(capture, '', None)
    st, ctl = cap.OpenCapture(rd.ReplayOptions(), None)
    draws = []
    walk(ctl.GetRootActions(), draws)
    found = []
    for a in draws:
        if event is not None and a.eventId != event:
            continue
        ctl.SetFrameEvent(a.eventId, False)
        pipe = ctl.GetPipelineState()
        vbs = pipe.GetVBuffers()
        if stride is not None and not (vbs and vbs[0].byteStride == stride):
            continue
        if min_width is not None and pipe.GetViewport(0).width < min_width:
            continue
        found.append(a)
    p('draws', len(draws), 'matching', len(found))
    targets = {}
    for a in found:
        ctl.SetFrameEvent(a.eventId, False)
        pipe = ctl.GetPipelineState()
        key = (tuple(str(r.resource) for r in pipe.GetOutputTargets()), str(pipe.GetDepthTarget().resource))
        targets[key] = targets.get(key, 0) + 1
    for k, v in targets.items():
        p('  targets', k, 'draws', v)

    history_done = os.environ.get('RDC_PIXEL_HISTORY') != '1'
    for n, a in enumerate(found):
        detail = n < dump
        ctl.SetFrameEvent(a.eventId, True)
        pipe = ctl.GetPipelineState()
        vp = pipe.GetViewport(0)
        positions = vs_positions(ctl)
        inside = []
        for x, y, z, w in positions:
            if w > 0 and -1 <= x / w <= 1 and -1 <= y / w <= 1 and 0 <= z / w <= 1:
                inside.append((x / w, y / w, z / w))
        p('event', a.eventId, 'indices', a.numIndices, 'instances', a.numInstances, 'baseVertex', a.baseVertex,
          'vertices', len(positions), 'on screen', len(inside),
          'ndc z %.4f..%.4f' % (min(v[2] for v in inside), max(v[2] for v in inside)) if inside else '')
        if detail:
            p('  viewport', vp.x, vp.y, vp.width, vp.height, 'depth', vp.minDepth, vp.maxDepth)
            ib = pipe.GetIBuffer()
            p('  index buffer', ib.resourceId, 'offset', ib.byteOffset, 'stride', ib.byteStride, 'first index', a.indexOffset)
            for i, vb in enumerate(pipe.GetVBuffers()):
                p('  vertex buffer', i, vb.resourceId, 'offset', vb.byteOffset, 'stride', vb.byteStride)
            for va in pipe.GetVertexInputs():
                p('    input', va.name, 'buffer', va.vertexBuffer, 'offset', va.byteOffset, va.format.Name(),
                  'per instance' if va.perInstance else '')
            refl = pipe.GetShaderReflection(rd.ShaderStage.Vertex)
            for r in pipe.GetReadOnlyResources(rd.ShaderStage.Vertex):
                p('    vertex stage resource', refl.readOnlyResources[r.access.index].name, r.descriptor.resource)
            for v in positions[:6]:
                p('    clip position', ['%.3f' % c for c in v])
        if not history_done and inside:
            history_done = True
            x, y, z = inside[len(inside) // 2]
            px = int(vp.x + (x * 0.5 + 0.5) * vp.width)
            py = int(vp.y + (y * 0.5 + 0.5) * vp.height)
            target = pipe.GetOutputTargets()[0].resource
            p('  pixel history at', px, py)
            for h in ctl.PixelHistory(target, px, py, rd.Subresource(0, 0, 0), rd.CompType.Typeless):
                if h.eventId != a.eventId:
                    continue
                flags = [f for f in ('backfaceCulled', 'depthClipped', 'viewClipped', 'scissorClipped',
                                     'shaderDiscarded', 'depthTestFailed', 'stencilTestFailed', 'sampleMasked')
                         if getattr(h, f, False)]
                p('    primitive', h.primitiveID, 'shader out', ['%.3f' % c for c in h.shaderOut.col.floatValue],
                  'after', ['%.3f' % c for c in h.postMod.col.floatValue], 'depth %.4f' % h.shaderOut.depth, flags)
    if found:
        ctl.SetFrameEvent(found[-1].eventId, True)
        save = rd.TextureSave()
        save.resourceId = ctl.GetPipelineState().GetOutputTargets()[0].resource
        save.destType = rd.FileType.PNG
        save.alpha = rd.AlphaMapping.Discard
        png = os.path.splitext(report)[0] + '-after.png'
        ctl.SaveTexture(save, png)
        p('colour target after the last matching draw:', png)
    ctl.Shutdown()
    cap.Shutdown()
except Exception:
    p(traceback.format_exc())
log.close()
os._exit(0)
