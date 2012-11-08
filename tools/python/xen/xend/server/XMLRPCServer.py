#============================================================================
# This library is free software; you can redistribute it and/or
# modify it under the terms of version 2.1 of the GNU Lesser General Public
# License as published by the Free Software Foundation.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#============================================================================
# Copyright (C) 2006 Anthony Liguori <aliguori@us.ibm.com>
# Copyright (C) 2006 XenSource Ltd.
#============================================================================

import errno
import socket
import types
import xmlrpclib
from xen.util.xmlrpclib2 import UnixXMLRPCServer, TCPXMLRPCServer
try:
    from SSLXMLRPCServer import SSLXMLRPCServer
    ssl_enabled = True
except ImportError:
    ssl_enabled = False

from xen.xend import XendAPI, XendDomain, XendDomainInfo, XendNode
from xen.xend import XendLogging, XendDmesg
from xen.xend.XendClient import XML_RPC_SOCKET
from xen.xend.XendConstants import DOM_STATE_RUNNING
from xen.xend.XendLogging import log
from xen.xend.XendError import XendInvalidDomain
from xen.xend.XendCPUPool import XendCPUPool

# vcpu_avail is a long and is not needed by the clients.  It's far easier
# to just remove it then to try and marshal the long.
def fixup_sxpr(sexpr):
    ret = []
    for k in sexpr:
        if type(k) in (list, tuple):
            if len(k) != 2 or k[0] != 'vcpu_avail':
                ret.append(fixup_sxpr(k))
        else:
            ret.append(k)
    return ret

def lookup(domid):
    info = XendDomain.instance().domain_lookup(domid)
    return info

def dispatch(domid, fn, args):
    info = lookup(domid)
    return getattr(info, fn)(*args)

def domain(domid, full = 0):
    info = lookup(domid)
    return fixup_sxpr(info.sxpr(not full))

def domains(detail = True, full = False):
    return domains_with_state(detail, DOM_STATE_RUNNING, full)

def domains_with_state(detail, state, full):
    if detail:
        domains = XendDomain.instance().list_sorted(state)
        ret = []
        for dom in domains:
            try:
                ret.append(fixup_sxpr(dom.sxpr(not full)))
            except:
                log.warn("Failed to query SXPR for domain %s" % str(dom))
                pass
        return ret
    else:
        return XendDomain.instance().list_names(state)

def domain_create(config):
    info = XendDomain.instance().domain_create(config)
    return fixup_sxpr(info.sxpr())

def domain_restore(src, paused=False):
    info = XendDomain.instance().domain_restore(src, paused)
    return fixup_sxpr(info.sxpr())

def get_log():
    f = open(XendLogging.getLogFilename(), 'r')
    try:
        return f.read()
    finally:
        f.close()

methods = ['device_create', 'device_configure',
           'destroyDevice','getDeviceSxprs',
           'setMemoryTarget', 'setName', 'setVCpuCount', 'shutdown',
           'send_sysrq', 'getVCPUInfo', 'waitForDevices',
           'getRestartCount', 'getBlockDeviceClass']

exclude = ['domain_create', 'domain_restore']

POOL_FUNCS = ['pool_create', 'pool_new', 'pool_start', 'pool_list',
              'pool_destroy', 'pool_delete', 'pool_cpu_add', 'pool_cpu_remove',
              'pool_migrate']

class XMLRPCServer:
    def __init__(self, auth, use_xenapi, use_tcp = False,
                 ssl_key_file = None, ssl_cert_file = None,
                 host = "localhost", port = 8006, path = XML_RPC_SOCKET,
                 hosts_allowed = None):
        
        self.use_tcp = use_tcp
        self.port = port
        self.host = host
        self.path = path
        self.hosts_allowed = hosts_allowed
        
        self.ssl_key_file = ssl_key_file
        self.ssl_cert_file = ssl_cert_file
        
        self.ready = False        
        self.running = True
        self.auth = auth
        self.xenapi = use_xenapi and XendAPI.XendAPI(auth) or None
        
    def run(self):
        authmsg = (self.auth == XendAPI.AUTH_NONE and 
                   "; authentication has been disabled for this server." or
                   ".")

        try:
            if self.use_tcp:
                using_ssl = self.ssl_key_file and self.ssl_cert_file

                log.info("Opening %s XML-RPC server on %s%d%s",
                         using_ssl and 'HTTPS' or 'TCP',
                         self.host and '%s:' % self.host or
                         'all interfaces, port ',
                         self.port, authmsg)

                if using_ssl:
                    if not ssl_enabled:
                        raise ValueError("pyOpenSSL not installed. "
                                         "Unable to start HTTPS XML-RPC server")
                    self.server = SSLXMLRPCServer(
                        (self.host, self.port),
                        self.hosts_allowed,
                        self.xenapi is not None,
                        logRequests = False,
                        ssl_key_file = self.ssl_key_file,
                        ssl_cert_file = self.ssl_cert_file)
                else:
                    self.server = TCPXMLRPCServer(
                        (self.host, self.port),
                        self.hosts_allowed,
                        self.xenapi is not None,
                        logRequests = False)

            else:
                log.info("Opening Unix domain socket XML-RPC server on %s%s",
                         self.path, authmsg)
                self.server = UnixXMLRPCServer(self.path, self.hosts_allowed,
                                               self.xenapi is not None,
                                               logRequests = False)
        except socket.error, exn:
            log.error('Cannot start server: %s!', exn.args[1])
            ready = True
            running = False
            return
        except Exception, e:
            log.exception('Cannot start server: %s!', e)
            ready = True
            running = False
            return
        
        # Register Xen API Functions
        # -------------------------------------------------------------------
        # exportable functions are ones that do not begin with '_'
        # and has the 'api' attribute.
        
        for meth_name in dir(self.xenapi):
            if meth_name[0] != '_':
                meth = getattr(self.xenapi, meth_name)
                if callable(meth) and hasattr(meth, 'api'):
                    self.server.register_function(meth, getattr(meth, 'api'))

        self.server.register_instance(XendAPI.XendAPIAsyncProxy(self.xenapi))
                
        # Legacy deprecated xm xmlrpc api
        # --------------------------------------------------------------------

        # Functions in XendDomainInfo
        for name in methods:
            fn = eval("lambda domid, *args: dispatch(domid, '%s', args)"%name)
            self.server.register_function(fn, "xend.domain.%s" % name)

        inst = XendDomain.instance()

        for name in dir(inst):
            fn = getattr(inst, name)
            if name.startswith("domain_") and callable(fn):
                if name not in exclude:
                    self.server.register_function(fn, "xend.domain.%s" % name[7:])

        # Functions in XendPool
        for name in POOL_FUNCS:
            fn = getattr(XendCPUPool, name)
            self.server.register_function(fn, "xend.cpu_pool.%s" % name[5:])

        # Functions in XendNode and XendDmesg
        for type, lst, n in [(XendNode,
                              ['info', 'pciinfo', 'send_debug_keys',
                               'tmem_list', 'tmem_freeze', 'tmem_thaw',
                               'tmem_flush', 'tmem_destroy', 'tmem_set_weight',
                               'tmem_set_cap', 'tmem_set_compress',
                               'tmem_query_freeable_mb', 'tmem_shared_auth'],
                             'node'),
                             (XendDmesg, ['info', 'clear'], 'node.dmesg')]:
            inst = type.instance()
            for name in lst:
                self.server.register_function(getattr(inst, name),
                                              "xend.%s.%s" % (n, name))

        # A few special cases
        self.server.register_function(domain, 'xend.domain')
        self.server.register_function(domains, 'xend.domains')
        self.server.register_function(domains_with_state,
                                      'xend.domains_with_state')
        self.server.register_function(get_log, 'xend.node.log')
        self.server.register_function(domain_create, 'xend.domain.create')
        self.server.register_function(domain_restore, 'xend.domain.restore')

        # A couple of the security functions
        from xen.util.xsm import xsm as security
        for name in security.xmlrpc_exports:
            fn = getattr(security, name)
            self.server.register_function(fn, "xend.security.%s" % name)

        self.server.register_introspection_functions()
        self.ready = True

        # Custom runloop so we can cleanup when exiting.
        # -----------------------------------------------------------------
        try:
            while self.running:
                self.server.handle_request()
        finally:
            self.shutdown()

    def cleanup(self):
        log.debug('XMLRPCServer.cleanup()')
        if hasattr(self, 'server'):
            try:
                # This is here to make sure the socket is actually
                # cleaned up when close() is called. Otherwise
                # SO_REUSEADDR doesn't take effect. To replicate,
                # try 'xend reload' and look for EADDRINUSE.
                #
                # May be caued by us calling close() outside of
                # the listen()ing thread.
                self.server.socket.shutdown(2)
            except socket.error, e:
                pass # ignore any socket errors
            try:
                self.server.socket.close()
            except socket.error, e:
                pass

    def shutdown(self):
        self.running = False
        if self.ready:
            self.ready = False
            self.cleanup()
