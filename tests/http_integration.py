#!/usr/bin/env python3
import json, math, pathlib, subprocess, sys, tempfile, time, urllib.request, urllib.error
server, orchestrator, client, config = sys.argv[1:]
with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)
    with (directory / 'worker.log').open('w') as log:
        worker = subprocess.Popen([server], stdout=log, stderr=log)
        try:
            def get(path):
                with urllib.request.urlopen('http://127.0.0.1:8080' + path, timeout=5) as response:
                    return response.read().decode()
            for _ in range(100):
                if worker.poll() is not None: raise RuntimeError('Worker exited; port 8080 must be free')
                try:
                    health = json.loads(get('/health'))
                    assert health['workload'] == 'catalan'
                    break
                except OSError: time.sleep(.05)
            else: raise RuntimeError('Worker did not start')
            for n in range(11):
                assert int(get('/binary-trees?n=' + str(n))) == math.comb(2*n,n)//(n+1)
            for value in ['', 'abc', '-1', '101', '2x', '999999999999999999999']:
                try: get('/binary-trees?n=' + value)
                except urllib.error.HTTPError as error: assert error.code == 400
                else: raise AssertionError('Invalid value accepted: ' + value)
        finally:
            worker.terminate()
            try: worker.wait(timeout=5)
            except subprocess.TimeoutExpired: worker.kill(); worker.wait()
    invalid = directory / 'invalid.json'
    invalid.write_text('{"vm":{"vcpus":-1}}')
    assert subprocess.run([orchestrator,str(invalid)],capture_output=True).returncode != 0
    assert subprocess.run([client,str(invalid)],capture_output=True).returncode != 0
print('HTTP validation, Catalan correctness, startup failures passed')
