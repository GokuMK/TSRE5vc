#!/usr/bin/env python3
import math,argparse,concurrent.futures,hashlib,json,os,pathlib,subprocess,time,threading
from compat_resume import prepare_resume
p=argparse.ArgumentParser();p.add_argument('--limit',type=int);p.add_argument('--workers',type=int,default=2);p.add_argument('--output',default='three-results');p.add_argument('--match');p.add_argument('--work',required=True,type=pathlib.Path);p.add_argument('--exe',type=pathlib.Path);a=p.parse_args()
base=a.work.absolute()
if str(base.resolve()).startswith('/mnt/'):raise SystemExit('Work directory must be on Linux')
project=pathlib.Path(__file__).resolve().parents[2]
rows=json.loads((base/'manifest.json').read_text())
if a.match:rows=[r for r in rows if a.match.lower() in r['path'].lower()]
if a.limit:rows=rows[:a.limit]
out=base/a.output
if not out.resolve().is_relative_to(base.resolve()):raise SystemExit('Output must remain inside work directory')
out.mkdir(exist_ok=True)
resultfile=out/'results.jsonl'
exe=str(a.exe.absolute() if a.exe else project/'build/TSRE5vc')
rows,provenance=prepare_resume(rows,resultfile,exe,__file__)
heavy=threading.Semaphore(1)
def run_case(row):
 start=time.monotonic();key=hashlib.sha256(row['path'].encode()).hexdigest()[:20];result={**provenance,'path':row['path'],'size':row['size'],'log_key':key}
 with open(row['source'],'rb') as f:
  result['sha256']=hashlib.file_digest(f,'sha256').hexdigest()
 for variant in ['legacy','compact','complete']:
  env=dict(os.environ,TSRE_THREE_COMPAT=variant,TSRE_COMPAT_ANIMATION_DURATION=str(result.get('legacy',{}).get('animation_duration',0)),LIBGL_ALWAYS_SOFTWARE='1',LP_NUM_THREADS='2',QT_HASH_SEED='0')
  if variant!='legacy' and 'bounds' in result['legacy']:
   env['TSRE_COMPAT_CAMERA']=json.dumps({k:result['legacy'][k] for k in ['size','bounds']})
  log=out/(key+'-'+variant+'.log');t=time.monotonic()
  cmd=['prlimit','--core=0','--as=3221225472','--fsize=33554432','--','bwrap','--die-with-parent','--bind','/','/','--ro-bind','/mnt','/mnt','--',exe,'--test','--test-suite','shape-complex-corpus-gl','--test-cases',row['mirror']]
  try:
   with log.open('wb') as output:
    proc=subprocess.run(cmd,cwd=project,env=env,stdout=output,stderr=subprocess.STDOUT,timeout=90)
   r={'exit_code':proc.returncode}
  except subprocess.TimeoutExpired:r={'timeout':True}
  r['wall_seconds']=time.monotonic()-t
  for line in log.read_text(errors='replace').splitlines():
   if line.startswith('COMPAT '):
    try:r.update(json.loads(line[7:]))
    except json.JSONDecodeError:pass
  r.pop('path',None);result[variant]=r
 def compare(a,b):
  differences=[]
  for key in ['loaded','lods','bounds','detail','snap','textures_ready','textures_missing_or_error','animation']:
   if a.get(key)!=b.get(key):differences.append(key)
  if not math.isclose(a.get('size',0),b.get('size',0),rel_tol=1e-6,abs_tol=1e-6):differences.append('size')
  la,lb=a.get('levels',[]),b.get('levels',[])
  if len(la)!=len(lb):differences.append('level_count')
  for i,(x,y) in enumerate(zip(la,lb)):
   for key in ['parents','matrices','parts','buffers','direct','gather','pick']:
    if x.get(key)!=y.get(key):differences.append(f'lod{i}.{key}')
   ta,tb=x.get('transforms',[]),y.get('transforms',[])
   if len(ta)!=len(tb) or any(len(u)!=len(v) or any(abs(p-q)>1e-5 for p,q in zip(u,v)) for u,v in zip(ta,tb)):
    differences.append(f'lod{i}.transforms')
  return differences
 success=lambda r:r.get('exit_code')==0 and r.get('stage')=='complete' and r.get('cpu_only') and r.get('gpu_ready')
 result['pairs']={}
 for va,vb in [('legacy','compact'),('legacy','complete'),('compact','complete')]:
  pair={'failures':[v for v in [va,vb] if not success(result[v])]}
  pair['differences']=compare(result[va],result[vb]) if not pair['failures'] else []
  pair['status']='failure' if pair['failures'] else 'difference' if pair['differences'] else 'match'
  result['pairs'][va+'_'+vb]=pair
 result['status']='failure' if any(p['status']=='failure' for p in result['pairs'].values()) else 'difference' if any(p['status']=='difference' for p in result['pairs'].values()) else 'match'
 stat=os.stat(row['source']);result['source_stat_unchanged']=(stat.st_size==row['size'] and stat.st_mtime_ns==row['mtime_ns'])
 result['wall_seconds']=time.monotonic()-start
 return result
def run(row):
 if row['size']>8*1024**2:
  with heavy:return run_case(row)
 return run_case(row)
with resultfile.open('a',buffering=1) as f,concurrent.futures.ThreadPoolExecutor(max_workers=a.workers) as pool:
 for i,result in enumerate(pool.map(run,rows),1):
  f.write(json.dumps(result)+'\n');print(i,'/',len(rows),result['status'],result['path'],flush=True)
