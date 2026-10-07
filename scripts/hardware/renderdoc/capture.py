# Runs inside qrenderdoc (--python, started by Capture-Frame.ps1): launches
# TSRE with RenderDoc injected, captures one frame after a delay, copies the
# capture to RDC_OUT\RDC_NAME.rdc and quits. Variables named RDC_CHILD_<name>
# are set for TSRE only (as <name>): in qrenderdoc's own environment, Qt 6
# plugin paths or RenderDoc's Vulkan layer break it.
import os, time, traceback
import renderdoc as rd

out = os.environ['RDC_OUT']
name = os.environ.get('RDC_NAME', 'tsre')
log = open(os.path.join(out, name + '.capture.txt'), 'w')
def p(*a):
    log.write(' '.join(str(x) for x in a) + '\n'); log.flush()

try:
    opts = rd.CaptureOptions()
    opts.refAllResources = True
    env = []
    for k, v in os.environ.items():
        if k.upper().startswith('RDC_CHILD_') and v:
            env.append(rd.EnvironmentModification(rd.EnvMod.Set, rd.EnvSep.NoSep, k[len('RDC_CHILD_'):], v))
    res = rd.ExecuteAndInject(os.environ['RDC_EXE'], os.environ['RDC_WORKDIR'], os.environ.get('RDC_ARGS', ''),
                              env, os.path.join(out, name), opts, False)
    p('inject', res.result, res.ident)
    tc = rd.CreateTargetControl('', res.ident, 'tsre-hardware', True)
    delay = float(os.environ.get('RDC_DELAY', '25'))
    start = time.time()
    triggered = False
    path = None
    while time.time() - start < delay + 60:
        msg = tc.ReceiveMessage(None)
        if msg.type == rd.TargetControlMessageType.Disconnected:
            p('disconnected'); break
        if msg.type == rd.TargetControlMessageType.RegisterAPI:
            p('api', msg.apiUse.name)
        if msg.type == rd.TargetControlMessageType.NewCapture:
            path = msg.newCapture.path
            p('capture', path, 'frame', msg.newCapture.frameNumber)
            break
        if not triggered and time.time() - start > delay:
            tc.TriggerCapture(1); triggered = True; p('triggered')
        time.sleep(0.05)
    if path:
        tc.CopyCapture(0, os.path.join(out, name + '.rdc'))
        for i in range(200):
            msg = tc.ReceiveMessage(None)
            if msg.type == rd.TargetControlMessageType.CaptureCopied:
                p('copied', msg.newCapture.path); break
            time.sleep(0.1)
    tc.Shutdown()
    # Ask TSRE to close, then make sure it did.
    os.system('taskkill /IM TSRE5vc.exe >NUL 2>&1')
    time.sleep(3)
    os.system('taskkill /F /IM TSRE5vc.exe >NUL 2>&1')
except Exception:
    p(traceback.format_exc())
log.close()
os._exit(0)
