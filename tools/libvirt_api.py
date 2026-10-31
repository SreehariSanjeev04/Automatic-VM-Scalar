"""Minimal read-only libvirt bindings used for calibration and acceptance checks."""
import ctypes as C
import ctypes.util
class Info(C.Structure):
    _fields_=[('state',C.c_ubyte),('maxMem',C.c_ulong),('memory',C.c_ulong),('vcpus',C.c_ushort),('cpuTime',C.c_ulonglong)]
class Hypervisor:
    def __init__(self, uri):
        self.lib=C.CDLL(ctypes.util.find_library('virt'))
        self.lib.virConnectOpen.argtypes=[C.c_char_p];self.lib.virConnectOpen.restype=C.c_void_p
        self.lib.virDomainLookupByName.argtypes=[C.c_void_p,C.c_char_p];self.lib.virDomainLookupByName.restype=C.c_void_p
        self.lib.virDomainGetInfo.argtypes=[C.c_void_p,C.POINTER(Info)]
        self.lib.virDomainFree.argtypes=[C.c_void_p];self.lib.virConnectClose.argtypes=[C.c_void_p]
        self.connection=self.lib.virConnectOpen(uri.encode())
        if not self.connection:raise RuntimeError('Cannot connect to libvirt')
    def info(self,name):
        domain=self.lib.virDomainLookupByName(self.connection,name.encode())
        if not domain:raise RuntimeError('Domain not found: '+name)
        try:
            result=Info()
            if self.lib.virDomainGetInfo(domain,C.byref(result))<0:raise RuntimeError('Cannot inspect '+name)
            return result
        finally:self.lib.virDomainFree(domain)
    def close(self):
        if self.connection:self.lib.virConnectClose(self.connection);self.connection=None
