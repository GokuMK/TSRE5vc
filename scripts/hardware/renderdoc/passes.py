# Runs inside qrenderdoc (Inspect-Capture.ps1 -Script passes): the GPU time
# of each render pass (RenderDoc's EventGPUDuration counter summed over the
# pass's events), its draws and its first colour target (size, format,
# layers), then the passes summed by target and the events outside passes
# (copies, clears, dispatches). Times are RenderDoc's replay, event by
# event: compare captures with each other, not with the frame log.
#
# RDC_PASSES: how many of the longest passes to list one by one (40).
import os, traceback
import renderdoc as rd

capture = os.environ['RDC_CAPTURE']
report = os.environ['RDC_REPORT']
log = open(report, 'w')
def p(*a):
    log.write(' '.join(str(x) for x in a) + '\n'); log.flush()

def flat(actions, out):
    for a in actions:
        out.append(a)
        flat(a.children, out)

try:
    listed = int(os.environ.get('RDC_PASSES', '40'))
    cap = rd.OpenCaptureFile()
    cap.OpenFile(capture, '', None)
    st, ctl = cap.OpenCapture(rd.ReplayOptions(), None)
    if rd.GPUCounter.EventGPUDuration not in ctl.EnumerateCounters():
        raise RuntimeError('no EventGPUDuration counter on this replay')
    desc = ctl.DescribeCounter(rd.GPUCounter.EventGPUDuration)
    durations = {}
    for r in ctl.FetchCounters([rd.GPUCounter.EventGPUDuration]):
        durations[r.eventId] = r.value.d if desc.resultByteWidth == 8 else r.value.f
    textures = {t.resourceId: t for t in ctl.GetTextures()}
    actions = []
    flat(ctl.GetRootActions(), actions)

    passes = []
    outside = {}
    current = None
    for a in actions:
        ms = durations.get(a.eventId, 0.0) * 1000.0
        if a.flags & rd.ActionFlags.BeginPass:
            current = {'begin': a.eventId, 'end': a.eventId, 'ms': ms, 'draws': 0, 'first': None}
            passes.append(current)
            continue
        if current is not None:
            current['ms'] += ms
            current['end'] = a.eventId
            if a.flags & rd.ActionFlags.Drawcall:
                current['draws'] += 1
                if current['first'] is None:
                    current['first'] = a.eventId
            if a.flags & rd.ActionFlags.EndPass:
                current = None
            continue
        if ms > 0.0:
            kind = a.customName if hasattr(a, 'customName') and a.customName else a.GetName(ctl.GetStructuredFile())
            kind = kind.split('(')[0].strip()
            outside[kind] = outside.get(kind, 0.0) + ms

    # Each pass's target, at its first draw (or its start).
    for ps in passes:
        ctl.SetFrameEvent(ps['first'] if ps['first'] is not None else ps['begin'], False)
        pipe = ctl.GetPipelineState()
        name = 'no colour'
        for t in pipe.GetOutputTargets():
            if t.resource != rd.ResourceId.Null():
                tex = textures.get(t.resource)
                if tex is not None:
                    name = '%dx%d %s%s' % (tex.width, tex.height, tex.format.Name(),
                                           ' x%d' % tex.arraysize if tex.arraysize > 1 else '')
                break
        if name == 'no colour':
            depth = pipe.GetDepthTarget()
            tex = textures.get(depth.resource)
            if tex is not None:
                name = 'depth %dx%d %s' % (tex.width, tex.height, tex.format.Name())
        ps['target'] = name

    total = sum(ps['ms'] for ps in passes) + sum(outside.values())
    p('passes', len(passes), 'events', len(actions), 'GPU total %.3f ms (in passes %.3f, outside %.3f)'
      % (total, sum(ps['ms'] for ps in passes), sum(outside.values())))
    p('')
    p('by target:')
    groups = {}
    for ps in passes:
        g = groups.setdefault(ps['target'], [0, 0.0, 0])
        g[0] += 1
        g[1] += ps['ms']
        g[2] += ps['draws']
    for name, (count, ms, draws) in sorted(groups.items(), key=lambda kv: -kv[1][1]):
        p('  %8.3f ms  %3d passes  %6d draws  %s' % (ms, count, draws, name))
    p('')
    p('outside passes:')
    for kind, ms in sorted(outside.items(), key=lambda kv: -kv[1]):
        p('  %8.3f ms  %s' % (ms, kind))
    p('')
    p('longest passes (events, draws, target):')
    for ps in sorted(passes, key=lambda ps: -ps['ms'])[:listed]:
        p('  %8.3f ms  events %d-%d  %5d draws  %s' % (ps['ms'], ps['begin'], ps['end'], ps['draws'], ps['target']))
    p('')
    p('in order:')
    for ps in passes:
        p('  %8.3f ms  events %d-%d  %5d draws  %s' % (ps['ms'], ps['begin'], ps['end'], ps['draws'], ps['target']))
    ctl.Shutdown()
    cap.Shutdown()
except Exception:
    p(traceback.format_exc())
log.close()
os._exit(0)
