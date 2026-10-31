#!/usr/bin/env python3
import concurrent.futures, csv, http.server, json, pathlib, socket, subprocess, sys, tempfile, threading, time, urllib.request, urllib.error
fixture, client, sample = sys.argv[1:]
class Worker(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200);self.end_headers();self.wfile.write(b'42')
    def log_message(self,*args): pass
servers=[]
with tempfile.TemporaryDirectory() as directory:
    directory=pathlib.Path(directory)
    # Bind dynamically chosen ports so tests do not require the real VM or port 8080.
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
    for address in ['127.0.0.1','127.0.0.2']:
        worker=http.server.ThreadingHTTPServer((address,port),Worker);servers.append(worker)
        threading.Thread(target=worker.serve_forever,daemon=True).start()
    config=json.loads(pathlib.Path(sample).read_text())
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0)); proxy_port=sock.getsockname()[1]
    config['vm']['guest_port']=port
    config['network'].update(load_balancer_host='127.0.0.1',load_balancer_port=proxy_port)
    config['traffic'].update(high_value=2,low_value=2,high_rate=30,low_rate=5)
    config['metrics_directory']=str(directory/'metrics');path=directory/'config.json';path.write_text(json.dumps(config))
    with (directory/'proxy.log').open('w') as log:
        proxy=subprocess.Popen([fixture,str(path)],stdin=subprocess.PIPE,stdout=log,stderr=log,text=True)
        generator=None
        try:
            base='http://127.0.0.1:'+str(proxy_port)
            def get(path):
                with urllib.request.urlopen(base+path,timeout=5) as response:return json.load(response)
            def post(value):
                request=urllib.request.Request(base+'/work',json.dumps(value).encode(),{'Content-Type':'application/json'})
                with urllib.request.urlopen(request,timeout=5) as response:return json.load(response)
            for _ in range(100):
                try: assert len(get('/replicas')['replicas'])==2;break
                except OSError: time.sleep(.05)
            else:raise AssertionError('Proxy did not start')
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:results=list(pool.map(lambda _:post({'value':2}),range(80)))
            assert sum(r['backend']=='a' for r in results)==40
            assert all(r['result']=='42' for r in results)
            for value in [{}, {'value':-1},{'value':101},{'value':2**80},{'value':'2'},[]]:
                try:post(value)
                except urllib.error.HTTPError as e:assert e.code==400
                else:raise AssertionError('Invalid payload accepted')
            generator=subprocess.Popen([client,str(path)],stdin=subprocess.PIPE,stdout=log,stderr=log,text=True)
            generator.stdin.write('increase\n');generator.stdin.flush();time.sleep(1.5)
            generator.stdin.write('decrease\n');generator.stdin.flush();time.sleep(1.5)
            proxy.stdin.write('drain\n');proxy.stdin.flush()
            for _ in range(100):
                if len(get('/replicas')['replicas'])==1:break
                time.sleep(.02)
            assert post({'value':2})['backend']=='a'
            generator.stdin.write('exit\n');generator.stdin.flush();assert generator.wait(timeout=10)==0
            assert set(next(csv.reader((directory/'results/request_metrics.csv').open())))=={'timestamp_ms','backend_ip','latency_ms','success','http_status'}
            rows=list(csv.DictReader((directory/'metrics/client.csv').open()))
            assert len(rows)>20 and all(r['status']=='200' for r in rows)
            events=list(csv.DictReader((directory/'metrics/client-events.csv').open()))
            assert [r['detail'] for r in events if r['event']=='mode']==['increase','decrease']
        finally:
            if generator and generator.poll() is None:generator.kill();generator.wait()
            proxy.stdin.write('exit\n');proxy.stdin.flush()
            try:proxy.wait(timeout=10)
            except subprocess.TimeoutExpired:proxy.kill();proxy.wait()
            for worker in servers:worker.shutdown();worker.server_close()
print('Concurrent routing, validation, membership, continuous traffic/mode-switch tests passed')
