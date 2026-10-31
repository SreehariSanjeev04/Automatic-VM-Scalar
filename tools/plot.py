#!/usr/bin/env python3
import argparse,csv,json,pathlib,statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
parser=argparse.ArgumentParser();parser.add_argument('run',type=pathlib.Path);args=parser.parse_args()
def rows(name):
    with (args.run/name).open() as f:return list(csv.DictReader(f))
cpu=rows('cpu.csv');client=rows('client.csv');events=rows('events.csv')
origin=min(float(r['timestamp']) for r in cpu)
fig,axes=plt.subplots(3,1,figsize=(12,10),sharex=True)
for name in sorted({r['name'] for r in cpu}):
    samples=[r for r in cpu if r['name']==name and r['valid']=='1']
    axes[0].plot([float(r['timestamp'])-origin for r in samples],[float(r['cpu_percent']) for r in samples],label=name)
axes[0].set_ylabel('CPU (%)');axes[0].legend();axes[0].axhline(75,color='gray',linestyle=':')
buckets={}
for r in client:buckets.setdefault(int(float(r['timestamp'])-origin),[]).append(r)
x=list(range(min(buckets),max(buckets)+1))
axes[1].plot(x,[statistics.mean(float(r['latency_ms']) for r in buckets[t]) if t in buckets else float('nan') for t in x],label='mean')
axes[1].set_ylabel('Latency (ms)')
axes[2].plot(x,[sum(200<=int(r['status'])<300 for r in buckets.get(t,[])) for t in x],label='successful requests/s')
axes[2].plot(x,[sum(not 200<=int(r['status'])<300 for r in buckets.get(t,[])) for t in x],label='failures/s')
axes[2].set_ylabel('Requests/s');axes[2].set_xlabel('Seconds');axes[2].legend()
for event in events:
    if event['event'] in ('scale_out_ready','scale_in_complete'):
        for axis in axes:axis.axvline(float(event['timestamp'])-origin,color='green',linestyle='--')
        axes[0].text(float(event['timestamp'])-origin,100,event['event'],rotation=90,va='top')
for axis in axes:axis.grid(alpha=.25)
fig.tight_layout();fig.savefig(args.run/'demo.png',dpi=180);fig.savefig(args.run/'demo.svg');print(args.run/'demo.png')
