#!/usr/bin/env python3
"""Offline copy through libvirt. Never copies a live VM disk; never overwrites a base."""
import argparse,ctypes as C,json,pathlib,time,xml.etree.ElementTree as ET
from libvirt_api import Hypervisor
parser=argparse.ArgumentParser();parser.add_argument('config',type=pathlib.Path);args=parser.parse_args()
config=json.loads(args.config.read_text());hypervisor=Hypervisor(config['libvirt']['uri']);lib=hypervisor.lib
for name,types,result in [
 ('virStoragePoolLookupByName',[C.c_void_p,C.c_char_p],C.c_void_p),
 ('virStoragePoolGetXMLDesc',[C.c_void_p,C.c_uint],C.c_void_p),
 ('virDomainGetXMLDesc',[C.c_void_p,C.c_uint],C.c_void_p),
 ('virStorageVolLookupByPath',[C.c_void_p,C.c_char_p],C.c_void_p),
 ('virStorageVolCreateXMLFrom',[C.c_void_p,C.c_char_p,C.c_void_p,C.c_uint],C.c_void_p),
 ('virStoragePoolRefresh',[C.c_void_p,C.c_uint],C.c_int),
 ('virDomainShutdown',[C.c_void_p],C.c_int),('virDomainCreate',[C.c_void_p],C.c_int),
 ('virStorageVolFree',[C.c_void_p],C.c_int),('virStoragePoolFree',[C.c_void_p],C.c_int)]:
    method=getattr(lib,name);method.argtypes=types;method.restype=result
libc=C.CDLL(None);libc.free.argtypes=[C.c_void_p]
def description(method,handle):
    pointer=method(handle,0)
    if not pointer:raise RuntimeError('Cannot read XML')
    try:return ET.fromstring(C.string_at(pointer))
    finally:libc.free(pointer)
domain=lib.virDomainLookupByName(hypervisor.connection,config['vm']['initial_name'].encode())
pool=lib.virStoragePoolLookupByName(hypervisor.connection,config['libvirt']['storage_pool'].encode())
if not domain or not pool:raise SystemExit('Domain and active pool must exist')
base=pathlib.Path(config['libvirt']['base_image']);target=description(lib.virStoragePoolGetXMLDesc,pool).findtext('target/path')
if base.parent!=pathlib.Path(target):raise SystemExit('Base must be inside the configured directory pool')
existing=lib.virStorageVolLookupByPath(hypervisor.connection,str(base).encode())
if existing:lib.virStorageVolFree(existing);raise SystemExit('Base already exists; refusing overwrite')
source_xml=description(lib.virDomainGetXMLDesc,domain)
source=next(d.find('source').get('file') for d in source_xml.findall('devices/disk') if d.get('device')=='disk')
was_running=hypervisor.info(config['vm']['initial_name']).state==1
stopped=False
try:
    if was_running:
        if lib.virDomainShutdown(domain)<0:raise RuntimeError('Cannot request guest shutdown')
        deadline=time.monotonic()+120
        while hypervisor.info(config['vm']['initial_name']).state!=5:
            if time.monotonic()>deadline:raise RuntimeError('Shutdown deadline; no forced poweroff performed')
            time.sleep(1)
    elif hypervisor.info(config['vm']['initial_name']).state!=5:raise RuntimeError('Guest must be running or shut off')
    stopped=True
    source_volume=lib.virStorageVolLookupByPath(hypervisor.connection,source.encode())
    if not source_volume:raise RuntimeError('Initial disk must be a managed volume')
    try:
        volume=ET.Element('volume');ET.SubElement(volume,'name').text=base.name
        ET.SubElement(volume,'capacity',{'unit':'bytes'}).text='0'
        target_xml=ET.SubElement(volume,'target');ET.SubElement(target_xml,'format',{'type':'qcow2'})
        copied=lib.virStorageVolCreateXMLFrom(pool,ET.tostring(volume),source_volume,0)
        if not copied:raise RuntimeError('Offline volume clone failed')
        lib.virStorageVolFree(copied);lib.virStoragePoolRefresh(pool,0)
    finally:lib.virStorageVolFree(source_volume)
    print('Created immutable base:',base,flush=True)
finally:
    if stopped and was_running:
        if lib.virDomainCreate(domain)<0:print('WARNING: original VM restart failed',flush=True)
    lib.virDomainFree(domain);lib.virStoragePoolFree(pool);hypervisor.close()
