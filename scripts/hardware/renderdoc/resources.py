# Runs inside qrenderdoc (Inspect-Capture.ps1 -Script resources): lists the
# capture's textures and buffers grouped by size and kind, largest first.
import os, traceback
import renderdoc as rd

capture = os.environ['RDC_CAPTURE']
log = open(os.environ['RDC_REPORT'], 'w')
def p(*a):
    log.write(' '.join(str(x) for x in a) + '\n'); log.flush()

try:
    cap = rd.OpenCaptureFile()
    cap.OpenFile(capture, '', None)
    st, ctl = cap.OpenCapture(rd.ReplayOptions(), None)
    texs = ctl.GetTextures()
    p('textures', len(texs), 'MB', round(sum(t.byteSize for t in texs) / 1048576, 1))
    groups = {}
    for t in texs:
        kind = ''
        if t.creationFlags & rd.TextureCategory.ColorTarget:
            kind += ' colour-target'
        if t.creationFlags & rd.TextureCategory.DepthTarget:
            kind += ' depth-target'
        key = '%dx%d %s mips %d layers %d%s' % (t.width, t.height, t.format.Name(), t.mips, t.arraysize, kind)
        g = groups.setdefault(key, [0, 0])
        g[0] += 1; g[1] += t.byteSize
    for k, v in sorted(groups.items(), key=lambda kv: -kv[1][1])[:40]:
        p('  %8.1f MB  x%-4d %s' % (v[1] / 1048576, v[0], k))
    # QRhi's Vulkan backend keeps a staging buffer as large as each Static
    # buffer: those show up as buffers with no usage flags.
    bufs = ctl.GetBuffers()
    p('buffers', len(bufs), 'MB', round(sum(b.length for b in bufs) / 1048576, 1))
    groups = {}
    for b in bufs:
        key = '%d bytes %s' % (b.length, str(b.creationFlags).replace('BufferCategory.', ''))
        g = groups.setdefault(key, [0, 0])
        g[0] += 1; g[1] += b.length
    for k, v in sorted(groups.items(), key=lambda kv: -kv[1][1])[:40]:
        p('  %8.1f MB  x%-4d %s' % (v[1] / 1048576, v[0], k))
    ctl.Shutdown()
    cap.Shutdown()
except Exception:
    p(traceback.format_exc())
log.close()
os._exit(0)
