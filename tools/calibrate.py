#!/usr/bin/env python3
import argparse, json, pathlib, statistics, time, urllib.request
from libvirt_api import Hypervisor
parser=argparse.ArgumentParser(description='Calibrate a fixed offered load against the existing Catalan guest')
parser.add_argument('config',type=pathlib.Path);parser.add_argument('--guest',default='192.168.122.86');parser.add_argument('--output',type=pathlib.Path)
args=parser.parse_args();args.config=args.config.resolve();config=json.loads(args.config.read_text())
base='http://%s:%s'%(args.guest,config['vm']['guest_port'])
def request(path):
    with urllib.request.urlopen(base+path,timeout=30) as response:return response.read()
assert json.loads(request('/health'))['workload']=='catalan','Provision the Catalan service first'
hypervisor=Hypervisor(config['libvirt']['uri'])
try:
    n=12
    for n in range(12,19):
        timings=[]
        for _ in range(3):
            start=time.monotonic();request('/binary-trees?n='+str(n));timings.append(time.monotonic()-start)
        print('n=%d median=%.4fs'%(n,statistics.median(timings)),flush=True)
        if statistics.median(timings)>=.05:break
    before=hypervisor.info(config['vm']['initial_name']).cpuTime
    for _ in range(20):request('/binary-trees?n='+str(n))
    cpu_seconds=(hypervisor.info(config['vm']['initial_name']).cpuTime-before)/1e9/20
    if cpu_seconds<=0:raise RuntimeError('No measurable CPU time')
finally:hypervisor.close()
config['traffic'].update(high_value=n,low_value=n,high_threads=8,high_rate=.90/cpu_seconds,low_rate=.09/cpu_seconds)
for key in ['state_file','metrics_directory']:
    value=pathlib.Path(config[key]);config[key]=str(value if value.is_absolute() else args.config.parent/value)
config['metrics_directory']=str(args.config.parent/'runs'/time.strftime('demo-%Y%m%d-%H%M%S'))
config['metrics']={'file_path':str(pathlib.Path(config['metrics_directory'])/'vm_metrics.csv')}
output=args.output or args.config.with_name('demo.config');output.write_text(json.dumps(config,indent=2)+'\n')
print(json.dumps({'input':n,'cpu_seconds_per_request':cpu_seconds,'high_rate':config['traffic']['high_rate'],'low_rate':config['traffic']['low_rate'],'output':str(output)},indent=2))
