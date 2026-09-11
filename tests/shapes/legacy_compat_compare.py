#!/usr/bin/env python3
import argparse,concurrent.futures,hashlib,json,os,pathlib,subprocess,time,threading
from compat_resume import prepare_resume
p=argparse.ArgumentParser();p.add_argument('--limit',type=int);p.add_argument('--workers',type=int,default=2);p.add_argument('--output',default='results');p.add_argument('--match');p.add_argument('--work',required=True,type=pathlib.Path);p.add_argument('--exe',type=pathlib.Path);a=p.parse_args()
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
 for variant in ['old','new']:
  env=dict(os.environ,TSRE_LEGACY_COMPAT=variant,LIBGL_ALWAYS_SOFTWARE='1',LP_NUM_THREADS='2',QT_HASH_SEED='0')
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
 old,new=result['old'],result['new']
 success=lambda r:r.get('exit_code')==0 and r.get('stage')=='complete'
 if success(old) and success(new):
  keys=['loaded','metadata_hash','lods','geometry','render','animation','texture_images','textures_ready','textures_missing_or_error']
  result['differences']=[k for k in keys if old.get(k)!=new.get(k)]
  result['status']='difference' if result['differences'] else 'match'
 elif success(old):result['status']='new_failure'
 elif success(new):result['status']='old_failure'
 elif old.get('exit_code')==new.get('exit_code')==0 and old.get('loaded')==new.get('loaded')==False:result['status']='both_rejected'
 else:result['status']='both_incomplete'
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
