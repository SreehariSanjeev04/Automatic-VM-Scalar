#!/usr/bin/env python3
import argparse,pathlib,subprocess,tarfile
parser=argparse.ArgumentParser();parser.add_argument('group');parser.add_argument('--results',type=pathlib.Path);parser.add_argument('--output',type=pathlib.Path);args=parser.parse_args()
if not args.group.replace('-','').replace('_','').isalnum():raise SystemExit('Use an alphanumeric group identifier')
root=pathlib.Path(__file__).resolve().parents[1];target=args.output or root/(args.group+'.tar.gz')
files=[root/name for name in ['CMakeLists.txt','README.md','autoscaler.config','.gitignore','main.cpp','plot_metrics.py','IMPLEMENTATION.md']]
for directory in ['autoscaler','client','commons','computation','logger','orchestrator','server','tests','tools','images']:
    files.extend(file for file in (root/directory).rglob('*') if file.is_file() and '__pycache__' not in file.parts)
for name in ['main.cpp','README.md','cloud-worker.service']:
    optional=root/'cloud-autoscalar'/'worker'/name
    if optional.is_file():files.append(optional)
with tarfile.open(target,'w:gz') as out:
    for file in files:
        out.add(file,arcname=args.group+'/'+str(file.relative_to(root)))
    if args.results:
        for file in args.results.rglob('*'):
            if file.is_file() and file.suffix in ('.csv','.json','.png','.svg','.log'):out.add(file,arcname=args.group+'/results/'+str(file.relative_to(args.results)))
print(target)
