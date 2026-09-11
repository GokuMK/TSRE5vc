// Benchmark harness only. The linked Open Rails parser sources are unchanged.
using System;
using System.Diagnostics;
using System.Linq;
using System.Text.Json;
using Orts.Formats.Msts;

static class Program
{
    static void Main(string[] args)
    {
        Trace.Listeners.Add(new TextWriterTraceListener(Console.Error));
        foreach (var path in args)
        {
            foreach (bool validate in new[]{false,true})
            {
                ShapeFile shape=null;
                var cold=Stopwatch.StartNew();
                shape=new ShapeFile(path,!validate);
                cold.Stop();
                for(int i=0;i<4;i++)shape=new ShapeFile(path,!validate);
                var times=new double[11];var allocated=new long[11];var live=new long[11];
                for(int i=0;i<times.Length;i++)
                {
                    shape=null;GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();
                    long startBytes=GC.GetAllocatedBytesForCurrentThread();
                    long startLive=GC.GetTotalMemory(false);
                    var timer=Stopwatch.StartNew();
                    shape=new ShapeFile(path,!validate);
                    timer.Stop();
                    times[i]=timer.Elapsed.TotalMilliseconds;
                    allocated[i]=GC.GetAllocatedBytesForCurrentThread()-startBytes;
                    live[i]=GC.GetTotalMemory(true)-startLive;
                    GC.KeepAlive(shape);
                }
                Console.WriteLine(JsonSerializer.Serialize(new {
                    path,validate,cold_ms=cold.Elapsed.TotalMilliseconds,samples_ms=times,
                    median_ms=times.OrderBy(x=>x).ElementAt(5),
                    allocated_bytes=allocated.OrderBy(x=>x).ElementAt(5),
                    retained_bytes=live.OrderBy(x=>x).ElementAt(5),
                    points=shape.shape.points.Count, matrices=shape.shape.matrices.Count,
                    lods=shape.shape.lod_controls.Sum(x=>x.distance_levels.Count),
                    subobjects=shape.shape.lod_controls.Sum(x=>x.distance_levels.Sum(y=>y.sub_objects.Count)),
                    runtime=System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription
                }));
            }
        }
    }
}
