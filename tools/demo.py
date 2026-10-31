#!/usr/bin/env python3
import argparse,csv,json,pathlib,subprocess,time,urllib.request
from libvirt_api import Hypervisor
parser=argparse.ArgumentParser(description='Run low → high → low with 1→2→1 replicas')
parser.add_argument('config',type=pathlib.Path);parser.add_argument('--build',type=pathlib.Path,default=pathlib.Path('build'));parser.add_argument('--attach',action='store_true')
parser.add_argument('--restart-check',action='store_true',help='Pause new traffic and restart the owned orchestrator with two replicas')
args=parser.parse_args();
if args.restart_check and args.attach:parser.error('--restart-check requires a demo-owned orchestrator')
args.config=args.config.resolve();config=json.loads(args.config.read_text())
root=args.config.parent;run=pathlib.Path(config['metrics_directory']);run=run if run.is_absolute() else root/run;run.mkdir(parents=True,exist_ok=True)
if (run/'client.csv').exists():raise SystemExit('Use a fresh metrics_directory for each demo')
base='http://%s:%d'%(config['network']['load_balancer_host'],config['network']['load_balancer_port'])
processes=[];logs=[];client=None
summary={'success':False,'phases':[]}
def membership():
    with urllib.request.urlopen(base+'/replicas',timeout=5) as response:return json.load(response)['replicas']
def phase(name):
    print(name,flush=True);summary['phases'].append({'name':name,'timestamp':time.time()})
def wait_count(count,timeout=300):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        if any(p.poll() is not None for p in processes):raise RuntimeError('A demo process exited; inspect logs')
        try:
            rows=membership()
            if len(rows)==count:return rows
        except OSError:pass
        time.sleep(1)
    raise RuntimeError('Replica-count deadline: '+str(count))
def hold(seconds):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        if any(p.poll() is not None for p in processes):raise RuntimeError('A demo process exited')
        time.sleep(min(1,max(0,deadline-time.monotonic())))
def command(text):client.stdin.write(text+'\n');client.stdin.flush()
try:
    if not args.attach:
        log=(run/'orchestrator.log').open('w');logs.append(log)
        processes.append(subprocess.Popen([str((args.build/'cloud_autoscalar').resolve()),str(args.config)],stdout=log,stderr=log))
    wait_count(1,180)
    log=(run/'client.log').open('w');logs.append(log)
    client=subprocess.Popen([str((args.build/'cloud_autoscalar_client').resolve()),str(args.config)],stdin=subprocess.PIPE,stdout=log,stderr=log,text=True);processes.append(client)
    phase('low_before');command('decrease');hold(60)
    phase('high_before');command('increase');replicas=wait_count(2)
    hypervisor=Hypervisor(config['libvirt']['uri'])
    try:
        for r in replicas:
            info=hypervisor.info(r['name']);assert info.vcpus==1 and info.maxMem==1024*1024
    finally:hypervisor.close()
    phase('high_after');hold(60)
    if args.restart_check:
        phase('restart_check');command('stop');hold(2)
        processes[0].terminate();processes[0].wait(timeout=150)
        log=(run/'orchestrator-restarted.log').open('w');logs.append(log)
        processes[0]=subprocess.Popen([str((args.build/'cloud_autoscalar').resolve()),str(args.config)],stdout=log,stderr=log)
        wait_count(2,180)
        command('increase');phase('high_resumed');hold(60)
    phase('low_after');command('decrease');wait_count(1)
    phase('scaled_in');hold(30);command('exit');client.wait(timeout=config['network']['client_timeout_seconds']+10)
    rows=list(csv.DictReader((run/'client.csv').open()))
    assert rows and all(200<=int(row['status'])<300 for row in rows),'Some requests failed'
    events=list(csv.DictReader((run/'client-events.csv').open()))
    assert not any(row['event']=='drop' for row in events),'Scheduler dropped requests'
    intervals={p['name']:p['timestamp'] for p in summary['phases']}
    cpu=list(csv.DictReader((run/'cpu.csv').open()))
    initial=config['vm']['initial_name']
    def mean_cpu(start,end,name):
        values=[float(r['cpu_percent']) for r in cpu if r['name']==name and r['valid']=='1' and start<=float(r['timestamp'])<end]
        if not values:raise AssertionError('Missing CPU samples')
        return sum(values)/len(values)
    before=mean_cpu(intervals['high_before'],intervals['high_after'],initial)
    after=mean_cpu(intervals['high_after']+10,intervals['low_after'],initial)
    assert after<before-10,'CPU did not fall meaningfully after scaling'
    for replica in replicas:
        assert any(row['backend']==replica['name'] and intervals['high_after']<float(row['timestamp'])<intervals['low_after'] for row in rows),'Replica received no requests'
    summary.update(success=True,initial_cpu_before=before,initial_cpu_after=after,requests=len(rows))
finally:
    if client and client.poll() is None:
        try:command('exit');client.wait(timeout=config['network']['client_timeout_seconds']+10)
        except (OSError,subprocess.TimeoutExpired):client.terminate()
    for process in reversed(processes):
        if process.poll() is None:
            process.terminate()
            try:process.wait(timeout=150)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    for log in logs:log.close()
    (run/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
