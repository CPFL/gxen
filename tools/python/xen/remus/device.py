# Remus device interface
#
# Coordinates with devices at suspend, resume, and commit hooks

import os, re, fcntl

import netlink, qdisc, util

class ReplicatedDiskException(Exception): pass
class BufferedNICException(Exception): pass

class CheckpointedDevice(object):
    'Base class for buffered devices'

    def postsuspend(self):
        'called after guest has suspended'
        pass

    def preresume(self):
        'called before guest resumes'
        pass

    def commit(self):
        'called when backup has acknowledged checkpoint reception'
        pass

class ReplicatedDisk(CheckpointedDevice):
    """
    Send a checkpoint message to a replicated disk while the domain
    is paused between epochs.
    """
    FIFODIR = '/var/run/tap'
    SEND_CHECKPOINT = 20
    WAIT_CHECKPOINT_ACK = 30

    def __init__(self, disk):
        # look up disk, make sure it is tap:buffer, and set up socket
        # to request commits.
        self.ctlfd = None
        self.msgfd = None
        self.is_drbd = False
        self.ackwait = False

        if disk.uname.startswith('tap:remus:') or disk.uname.startswith('tap:tapdisk:remus:'):
            fifo = re.match("tap:.*(remus.*)\|", disk.uname).group(1).replace(':', '_')
            absfifo = os.path.join(self.FIFODIR, fifo)
            absmsgfifo = absfifo + '.msg'

            self.installed = False
            self.ctlfd = open(absfifo, 'w+b')
            self.msgfd = open(absmsgfifo, 'r+b')
        elif disk.uname.startswith('drbd:'):
            #get the drbd device associated with this resource
            drbdres = re.match("drbd:(.*)", disk.uname).group(1)
            drbddev = util.runcmd("drbdadm sh-dev %s" % drbdres).rstrip()

            #check for remus supported drbd installation
            rconf = util.runcmd("drbdsetup %s show" % drbddev)
            if rconf.find('protocol D;') == -1:
                raise ReplicatedDiskException('Remus support for DRBD disks requires the '
                                              'resources to operate in protocol D. Please make '
                                              'sure that you have installed the remus supported DRBD '
                                              'version from git://aramis.nss.cs.ubc.ca/drbd-8.3-remus '
                                              'and enabled protocol D in the resource config')

            #check if resource is in connected state
            cstate = util.runcmd("drbdadm cstate %s" % drbdres).rstrip()
            if cstate != 'Connected':
                raise ReplicatedDiskException('DRBD resource %s is not in connected state!'
                                              % drbdres)

            #open a handle to the resource so that we could issue chkpt ioctls
            self.ctlfd = open(drbddev, 'r')
            self.is_drbd = True
        else:
            raise ReplicatedDiskException('Disk is not replicated: %s' %
                                        str(disk))

    def __del__(self):
        self.uninstall()

    def uninstall(self):
        if self.ctlfd:
            self.ctlfd.close()
            self.ctlfd = None

    def postsuspend(self):
        if not self.is_drbd:
            os.write(self.ctlfd.fileno(), 'flush')
        elif not self.ackwait:
            if (fcntl.ioctl(self.ctlfd.fileno(), self.SEND_CHECKPOINT, 0) > 0):
                self.ackwait = False
            else:
                self.ackwait = True

    def preresume(self):
        if self.is_drbd and self.ackwait:
            fcntl.ioctl(self.ctlfd.fileno(), self.WAIT_CHECKPOINT_ACK, 0)
            self.ackwait = False

    def commit(self):
        if not self.is_drbd:
            msg = os.read(self.msgfd.fileno(), 4)
            if msg != 'done':
                print 'Unknown message: %s' % msg

### Network

# shared rtnl handle
_rth = None
def getrth():
    global _rth

    if not _rth:
        _rth = netlink.rtnl()
    return _rth

class Netbuf(object):
    "Proxy for netdev with a queueing discipline"

    @staticmethod
    def devclass():
        "returns the name of this device class"
        return 'unknown'

    @classmethod
    def available(cls):
        "returns True if this module can proxy the device"
        return cls._hasdev(cls.devclass())

    def __init__(self, devname):
        self.devname = devname
        self.vif = None

    # override in subclasses
    def install(self, vif):
        "set up proxy on device"
        raise BufferedNICException('unimplemented')

    def uninstall(self):
        "remove proxy on device"
        raise BufferedNICException('unimplemented')

    # protected
    @staticmethod
    def _hasdev(devclass):
        """check for existence of device, attempting to load kernel
        module if not present"""
        devname = '%s0' % devclass
        rth = getrth()

        if rth.getlink(devname):
            return True
        if util.modprobe(devclass) and rth.getlink(devname):
            return True

        return False

class IFBBuffer(Netbuf):
    """Capture packets arriving on a VIF using an ingress filter and tc
    mirred action to forward them to an IFB device.
    """

    @staticmethod
    def devclass():
        return 'ifb'

    def install(self, vif):
        self.vif = vif
        # voodoo from http://www.linuxfoundation.org/collaborate/workgroups/networking/ifb#Typical_Usage
        util.runcmd('ip link set %s up' % self.devname)
        try:
            util.runcmd('tc qdisc add dev %s ingress' % vif.dev)
        except util.PipeException, e:
            # check if error indicates that ingress qdisc
            # already exists on the vif. If so, ignore it.
            ignoreme = 'RTNETLINK answers: File exists'
            if ignoreme in str(e):
                pass
            else:
                raise e
        util.runcmd('tc filter add dev %s parent ffff: proto ip pref 10 '
                    'u32 match u32 0 0 action mirred egress redirect '
                    'dev %s' % (vif.dev, self.devname))

    def uninstall(self):
        try:
            util.runcmd('tc qdisc del dev %s ingress' % self.vif.dev)
        except util.PipeException, e:
            pass
        util.runcmd('ip link set %s down' % self.devname)

class IMQBuffer(Netbuf):
    """Redirect packets coming in on vif to an IMQ device."""

    imqebt = '/usr/lib/xen/bin/imqebt'

    @staticmethod
    def devclass():
        return 'imq'

    def install(self, vif):
        # stopgap hack to set up IMQ for an interface. Wrong in many ways.
        self.vif = vif

        for mod in ['imq', 'ebt_imq']:
            util.runcmd(['modprobe', mod])
        util.runcmd("ip link set %s up" % self.devname)
        util.runcmd("%s -F FORWARD" % self.imqebt)
        util.runcmd("%s -A FORWARD -i %s -j imq --todev %s" % (self.imqebt, vif.dev, self.devname))

    def uninstall(self):
        util.runcmd("%s -F FORWARD" % self.imqebt)
        util.runcmd('ip link set %s down' % self.devname)

# in order of desirability
netbuftypes = [IFBBuffer, IMQBuffer]

def selectnetbuf():
    "Find the best available buffer type"
    for driver in netbuftypes:
        if driver.available():
            return driver

    raise BufferedNICException('no net buffer available')

class Netbufpool(object):
    """Allocates/releases proxy netdevs (IMQ/IFB)

    A file contains a list of entries of the form <pid>:<device>\n
    To allocate a device, lock the file, then claim a new device if
    one is free. If there are no free devices, check each PID for liveness
    and take a device if the PID is dead, otherwise return failure.
    Add an entry to the file before releasing the lock.
    """
    def __init__(self, netbufclass):
        "Create a pool of Device"
        self.netbufclass = netbufclass
        self.path = '/var/run/remus/' + self.netbufclass.devclass()

        self.devices = self.getdevs()

        pooldir = os.path.dirname(self.path)
        if not os.path.exists(pooldir):
            os.makedirs(pooldir, 0755)

    def get(self):
        "allocate a free device"
        def getfreedev(table):
            for dev in self.devices:
                if dev not in table or not util.checkpid(table[dev]):
                    return dev

            return None

        lock = util.Lock(self.path)
        table = self.load()

        dev = getfreedev(table)
        if not dev:
            lock.unlock()
            raise BufferedNICException('no free devices')
        dev = self.netbufclass(dev)

        table[dev.devname] = os.getpid()

        self.save(table)
        lock.unlock()

        return dev

    def put(self, dev):
        "release claim on device"
        lock = util.Lock(self.path)
        table = self.load()

        del table[dev.devname]

        self.save(table)
        lock.unlock()

    # private
    def load(self):
        """load and parse allocation table"""
        table = {}
        if not os.path.exists(self.path):
            return table

        fd = open(self.path)
        for line in fd.readlines():
            iface, pid = line.strip().split()
            table[iface] = int(pid)
        fd.close()
        return table

    def save(self, table):
        """write table to disk"""
        lines = ['%s %d\n' % (iface, table[iface]) for iface in sorted(table)]
        fd = open(self.path, 'w')
        fd.writelines(lines)
        fd.close()

    def getdevs(self):
        """find all available devices of our device type"""
        ifaces = []
        for line in util.runcmd('ifconfig -a -s').splitlines():
            iface = line.split()[0]
            if iface.startswith(self.netbufclass.devclass()):
                ifaces.append(iface)

        return ifaces

class BufferedNIC(CheckpointedDevice):
    """
    Buffer a protected domain's network output between rounds so that
    nothing is issued that a failover might not know about.
    """

    def __init__(self, vif):
        self.installed = False
        self.vif = vif

        self.pool = Netbufpool(selectnetbuf())
        self.rth = getrth()

        self.setup()

    def __del__(self):
        self.uninstall()

    def postsuspend(self):
        if not self.installed:
            self.install()

        self._sendqmsg(qdisc.TC_PLUG_CHECKPOINT)

    def commit(self):
        '''Called when checkpoint has been acknowledged by
        the backup'''
        self._sendqmsg(qdisc.TC_PLUG_RELEASE)

    # private
    def _sendqmsg(self, action):
        self.q.action = action
        req = qdisc.changerequest(self.bufdevno, self.handle, self.q)
        self.rth.talk(req.pack())
        return True

    def setup(self):
        """install Remus plug on VIF outbound traffic"""
        self.bufdev = self.pool.get()

        devname = self.bufdev.devname
        bufdev = self.rth.getlink(devname)
        if not bufdev:
            raise BufferedNICException('could not find device %s' % devname)

        self.bufdev.install(self.vif)

        self.bufdevno = bufdev['index']
        self.handle = qdisc.TC_H_ROOT
        self.q = qdisc.PlugQdisc()

        if not util.modprobe('sch_plug'):
            raise BufferedNICException('could not load sch_plug module')

    def install(self):
        devname = self.bufdev.devname
        q = self.rth.getqdisc(self.bufdevno)
        if q:
            if q['kind'] == 'plug':
                self.installed = True
                return
            if q['kind'] not in ('ingress', 'pfifo_fast', 'mq'):
                raise BufferedNICException('there is already a queueing '
                                           'discipline %s on %s' % (q['kind'], devname))

        print ('installing buffer on %s... ' % devname),
        req = qdisc.addrequest(self.bufdevno, self.handle, self.q)
        self.rth.talk(req.pack())
        self.installed = True
        print 'done.'

    def uninstall(self):
        if self.installed:
            try:
                req = qdisc.delrequest(self.bufdevno, self.handle)
                self.rth.talk(req.pack())
            except IOError, e:
                pass
            self.installed = False

            try:
                self.bufdev.uninstall()
            except util.PipeException, e:
                pass
            self.pool.put(self.bufdev)
