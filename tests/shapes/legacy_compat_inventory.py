#!/usr/bin/env python3
import os,json,time,argparse
from pathlib import Path
parser=argparse.ArgumentParser(description="Create a Linux-only case-normalized symlink view; source is only read.")
parser.add_argument('--source',required=True,type=Path)
parser.add_argument('--work',required=True,type=Path)
args=parser.parse_args()
source=args.source.absolute();work=args.work.absolute()
if str(work.resolve()).startswith('/mnt/') or work.resolve().is_relative_to(source.resolve()):
 raise SystemExit('Work directory must be on Linux and outside the source collection')
work.mkdir(parents=True,exist_ok=True)
dest=work/'trainset';dest.mkdir(exist_ok=True)
rows=[];files=0;links=[];collisions=[]; started=time.monotonic()
for base,dirs,names in os.walk(source,followlinks=False):
 rel=Path(base).relative_to(source);target=dest/str(rel).lower();target.mkdir(parents=True,exist_ok=True)
 for d in dirs:
  if (Path(base)/d).is_symlink():links.append(str((Path(base)/d).relative_to(source)))
 for name in names:
  original=Path(base)/name;link=target/name.lower()
  if link.is_symlink():
   if str(link.readlink())!=str(original):collisions.append(str(original))
  elif not link.exists():link.symlink_to(original)
  files+=1
  if original.suffix.lower()=='.s':
   stat=original.stat()
   rows.append({'path':str(original.relative_to(source)),'source':str(original),'mirror':str(link),'size':stat.st_size,'mtime_ns':stat.st_mtime_ns})
rows.sort(key=lambda r:r['path'].lower())
report={'shapes':len(rows),'files':files,'linked_directories_not_followed':links,'case_collisions':collisions,'seconds':time.monotonic()-started}
(work/'manifest.json').write_text(json.dumps(rows,indent=2))
(work/'inventory.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
