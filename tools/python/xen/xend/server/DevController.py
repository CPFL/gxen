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
# Copyright (C) 2004, 2005 Mike Wray <mike.wray@hp.com>
# Copyright (C) 2005 XenSource Ltd
#============================================================================

from threading import Event
import types

from xen.xend import sxp, XendOptions
from xen.xend.XendError import VmError
from xen.xend.XendLogging import log
import xen.xend.XendConfig
from xen.xend.server.DevConstants import *

from xen.xend.xenstore.xstransact import xstransact, complete
from xen.xend.xenstore.xswatch import xswatch
import xen.xend.server.DevConstants
import os, re

xoptions = XendOptions.instance()


class DevController:
    """Abstract base class for a device controller.  Device controllers create
    appropriate entries in the store to trigger the creation, reconfiguration,
    and destruction of devices in guest domains.  Each subclass of
    DevController is responsible for a particular device-class, and
    understands the details of configuration specific to that device-class.

    DevController itself provides the functionality common to all device
    creation tasks, as well as providing an interface to XendDomainInfo for
    triggering those events themselves.
    """

    # Set when registered.
    deviceClass = None


    ## public:

    def __init__(self, vm):
        self.vm = vm
        self.hotplug = True

    def createDevice(self, config):
        """Trigger the creation of a device with the given configuration.

        @return The ID for the newly created device.
        """
        (devid, back, front) = self.getDeviceDetails(config)
        if devid is None:
            return 0

        self.setupDevice(config)

        (backpath, frontpath) = self.addStoreEntries(config, devid, back,
                                                     front)

        import xen.xend.XendDomain
        xd = xen.xend.XendDomain.instance()
        backdom_name = config.get('backend')
        if backdom_name is None:
            backdom = xen.xend.XendDomain.DOM0_ID
        else:
            bd = xd.domain_lookup_nr(backdom_name)
            backdom = bd.getDomid()
        count = 0
        while True:
            t = xstransact()
            try:
                if devid in self.deviceIDs(t):
                    if 'dev' in back:
                        dev_str = '%s (%d, %s)' % (back['dev'], devid,
                                                   self.deviceClass)
                    else:
                        dev_str = '%s (%s)' % (devid, self.deviceClass)
                    
                    raise VmError("Device %s is already connected." % dev_str)

                if count == 0:
                    log.debug('DevController: writing %s to %s.',
                              str(front), frontpath)
                    log.debug('DevController: writing %s to %s.',
                              str(xen.xend.XendConfig.scrub_password(back)), backpath)
                elif count % 50 == 0:
                    log.debug(
                      'DevController: still waiting to write device entries.')

                devpath = self.devicePath(devid)

                t.remove(frontpath)
                t.remove(backpath)
                t.remove(devpath)

                t.mkdir(backpath)
                t.set_permissions(backpath,
                                  {'dom': backdom },
                                  {'dom'  : self.vm.getDomid(),
                                   'read' : True })
                t.mkdir(frontpath)
                t.set_permissions(frontpath,
                                  {'dom': self.vm.getDomid()},
                                  {'dom': backdom, 'read': True})

                t.write2(frontpath, front)
                t.write2(backpath,  back)

                t.mkdir(devpath)
                t.write2(devpath, {
                    'backend' : backpath,
                    'backend-id' : "%i" % backdom,
                    'frontend' : frontpath,
                    'frontend-id' : "%i" % self.vm.getDomid()
                })

                if t.commit():
                    return devid

                count += 1
            except:
                t.abort()
                raise


    def waitForDevices(self):
        log.debug("Waiting for devices %s.", self.deviceClass)
        return map(self.waitForDevice, self.deviceIDs())


    def waitForDevice(self, devid):
        log.debug("Waiting for %s.", devid)

        if not self.hotplug:
            return

        (status, err) = self.waitForBackend(devid)

        if status == Timeout:
            self.destroyDevice(devid, False)
            raise VmError("Device %s (%s) could not be connected. "
                          "Hotplug scripts not working." %
                          (devid, self.deviceClass))

        elif status == Error:
            self.destroyDevice(devid, False)
            if err is None:
                raise VmError("Device %s (%s) could not be connected. "
                              "Backend device not found." %
                              (devid, self.deviceClass))
            else:
                raise VmError("Device %s (%s) could not be connected. "
                              "%s" % (devid, self.deviceClass, err))
        elif status == Missing:
            # Don't try to destroy the device; it's already gone away.
            raise VmError("Device %s (%s) could not be connected. "
                          "Device not found." % (devid, self.deviceClass))

        elif status == Busy:
            self.destroyDevice(devid, False)
            if err is None:
                err = "Busy."
            raise VmError("Device %s (%s) could not be connected.\n%s" %
                          (devid, self.deviceClass, err))


    def waitForDevice_destroy(self, devid, backpath):
        log.debug("Waiting for %s - destroyDevice.", devid)

        if not self.hotplug:
            return

        status = self.waitForBackend_destroy(backpath)

        if status == Timeout:
            raise VmError("Device %s (%s) could not be disconnected. " %
                          (devid, self.deviceClass))

    def waitForDevice_reconfigure(self, devid):
        log.debug("Waiting for %s - reconfigureDevice.", devid)

        (status, err) = self.waitForBackend_reconfigure(devid)

        if status == Timeout:
            raise VmError("Device %s (%s) could not be reconfigured. " %
                          (devid, self.deviceClass))


    def reconfigureDevice(self, devid, config):
        """Reconfigure the specified device.

        The implementation here just raises VmError.  This may be overridden
        by those subclasses that can reconfigure their devices.
        """
        raise VmError('%s devices may not be reconfigured' % self.deviceClass)


    def destroyDevice(self, devid, force):
        """Destroy the specified device.

        @param devid The device ID, or something device-specific from which
        the device ID can be determined (such as a guest-side device name).

        The implementation here simply deletes the appropriate paths from the
        store.  This may be overridden by subclasses who need to perform other
        tasks on destruction. The implementation here accepts integer device
        IDs or paths containg integer deviceIDs, e.g. vfb/0.  Subclasses may
        accept other values and convert them to integers before passing them
        here.
        """

        dev = self.convertToDeviceNumber(devid)

        # Modify online status /before/ updating state (latter is watched by
        # drivers, so this ordering avoids a race).
        self.writeBackend(dev, 'online', "0")
        self.writeBackend(dev, 'state', str(xenbusState['Closing']))

        if force:
            frontpath = self.frontendPath(dev)
            backpath = self.readVm(dev, "backend")
            if backpath:
                xstransact.Remove(backpath)
            xstransact.Remove(frontpath)

            # xstransact.Remove(self.devicePath()) ?? Below is the same ?
            self.vm._removeVm("device/%s/%d" % (self.deviceClass, dev))

    def configurations(self, transaction = None):
        return map(lambda x: self.configuration(x, transaction), self.deviceIDs(transaction))


    def configuration(self, devid, transaction = None):
        """@return an s-expression giving the current configuration of the
        specified device.  This would be suitable for giving to {@link
        #createDevice} in order to recreate that device."""
        configDict = self.getDeviceConfiguration(devid, transaction)
        sxpr = [self.deviceClass]
        for key, val in configDict.items():
            if isinstance(val, (types.ListType, types.TupleType)):
                for v in val:
                    if v != None:
                        sxpr.append([key, v])
            else:
                if val != None:
                    sxpr.append([key, val])
        return sxpr

    def sxprs(self):
        """@return an s-expression describing all the devices of this
        controller's device-class.
        """
        return xstransact.ListRecursive(self.frontendRoot())


    def sxpr(self, devid):
        """@return an s-expression describing the specified device.
        """
        return [self.deviceClass, ['dom', self.vm.getDomid(),
                                   'id', devid]]


    def getDeviceConfiguration(self, devid, transaction = None):
        """Returns the configuration of a device.

        @note: Similar to L{configuration} except it returns a dict.
        @return: dict
        """
        if transaction is None:
            backdomid = xstransact.Read(self.devicePath(devid), "backend-id")
        else:
            backdomid = transaction.read(self.devicePath(devid) + "/backend-id")

        if backdomid is None:
            raise VmError("Device %s not connected" % devid)

        return {'backend': int(backdomid)}

    def getAllDeviceConfigurations(self):
        all_configs = {}
        for devid in self.deviceIDs():
            config_dict = self.getDeviceConfiguration(devid)
            all_configs[devid] = config_dict
        return all_configs


    def convertToDeviceNumber(self, devid):
        try:
            return int(devid)
        except ValueError:
            # Does devid contain devicetype/deviceid?
            # Propogate exception if unable to find an integer devid
            return int(type(devid) is str and devid.split('/')[-1] or None)

    ## protected:

    def getDeviceDetails(self, config):
        """Compute the details for creation of a device corresponding to the
        given configuration.  These details consist of a tuple of (devID,
        backDetails, frontDetails), where devID is the ID for the new device,
        and backDetails and frontDetails are the device configuration
        specifics for the backend and frontend respectively.

        backDetails and frontDetails should be dictionaries, the keys and
        values of which will be used as paths in the store.  There is no need
        for these dictionaries to include the references from frontend to
        backend, nor vice versa, as these will be handled by DevController.

        Abstract; must be implemented by every subclass.

        @return (devID, backDetails, frontDetails), as specified above.
        """

        raise NotImplementedError()

    def setupDevice(self, config):
        """ Setup device from config.
        """
        return

    def migrate(self, deviceConfig, network, dst, step, domName):
        """ Migration of a device. The 'network' parameter indicates
            whether the device is network-migrated (True). 'dst' then gives
            the hostname of the machine to migrate to.
        This function is called for 4 steps:
        If step == 0: Check whether the device is ready to be migrated
                      or can at all be migrated; return a '-1' if
                      the device is NOT ready, a '0' otherwise. If it is
                      not ready ( = not possible to migrate this device),
                      migration will not take place.
           step == 1: Called immediately after step 0; migration
                      of the kernel has started;
           step == 2: Called after the suspend has been issued
                      to the domain and the domain is not scheduled anymore.
                      Synchronize with what was started in step 1, if necessary.
                      Now the device should initiate its transfer to the
                      given target. Since there might be more than just
                      one device initiating a migration, this step should
                      put the process performing the transfer into the
                      background and return immediately to achieve as much
                      concurrency as possible.
           step == 3: Synchronize with the migration of the device that
                      was initiated in step 2.
                      Make sure that the migration has finished and only
                      then return from the call.
        """
        tool = xoptions.get_external_migration_tool()
        if tool:
            log.info("Calling external migration tool for step %d" % step)
            fd = os.popen("%s -type %s -step %d -host %s -domname %s" %
                          (tool, self.deviceClass, step, dst, domName))
            for line in fd:
                log.info(line.rstrip())
            rc = fd.close()
            if rc:
                raise VmError('Migration tool returned %d' % (rc >> 8))
        return 0


    def recover_migrate(self, deviceConfig, network, dst, step, domName):
        """ Recover from device migration. The given step was the
            last one that was successfully executed.
        """
        tool = xoptions.get_external_migration_tool()
        if tool:
            log.info("Calling external migration tool")
            fd = os.popen("%s -type %s -step %d -host %s -domname %s -recover" %
                          (tool, self.deviceClass, step, dst, domName))
            for line in fd:
                log.info(line.rstrip())
            rc = fd.close()
            if rc:
                raise VmError('Migration tool returned %d' % (rc >> 8))
        return 0


    def getDomid(self):
        """Stub to {@link XendDomainInfo.getDomid}, for use by our
        subclasses.
        """
        return self.vm.getDomid()


    def allocateDeviceID(self):
        """Allocate a device ID, allocating them consecutively on a
        per-domain, per-device-class basis, and using the store to record the
        next available ID.

        This method is available to our subclasses, though it is not
        compulsory to use it; subclasses may prefer to allocate IDs based upon
        the device configuration instead.
        """
        path = self.frontendMiscPath()
        return complete(path, self._allocateDeviceID)


    def _allocateDeviceID(self, t):
        result = t.read("nextDeviceID")
        if result:
            result = int(result)
        else:
            result = 0
        t.write("nextDeviceID", str(result + 1))
        return result


    def removeBackend(self, devid, *args):
        frontpath = self.frontendPath(devid)
        backpath = xstransact.Read(frontpath, "backend")
        if backpath:
            return xstransact.Remove(backpath, *args)
        else:
            raise VmError("Device %s not connected" % devid)

    def readVm(self, devid, *args):
        devpath = self.devicePath(devid)
        if devpath:
            return xstransact.Read(devpath, *args)
        else:
            raise VmError("Device config %s not found" % devid)

    def readBackend(self, devid, *args):
        backpath = self.readVm(devid, "backend")
        if backpath:
            return xstransact.Read(backpath, *args)
        else:
            raise VmError("Device %s not connected" % devid)

    def readBackendTxn(self, transaction, devid, *args):
        backpath = self.readVm(devid, "backend")
        if backpath:
            paths = map(lambda x: backpath + "/" + x, args)
            return transaction.read(*paths)
        else:
            raise VmError("Device %s not connected" % devid)

    def readFrontend(self, devid, *args):
        return xstransact.Read(self.frontendPath(devid), *args)

    def readFrontendTxn(self, transaction, devid, *args):
        paths = map(lambda x: self.frontendPath(devid) + "/" + x, args)
        return transaction.read(*paths)

    def deviceIDs(self, transaction = None):
        """@return The IDs of each of the devices currently configured for
        this instance's deviceClass.
        """
        fe = self.deviceRoot()

        if transaction:
            return map(lambda x: int(x.split('/')[-1]), transaction.list(fe))
        else:
            return map(int, xstransact.List(fe))


    def writeBackend(self, devid, *args):
        backpath = self.readVm(devid, "backend")

        if backpath:
            xstransact.Write(backpath, *args)
        else:
            raise VmError("Device %s not connected" % devid)


## private:

    def addStoreEntries(self, config, devid, backDetails, frontDetails):
        """Add to backDetails and frontDetails the entries to be written in
        the store to trigger creation of a device.  The backend domain ID is
        taken from the given config, paths for frontend and backend are
        computed, and these are added to the backDetails and frontDetails
        dictionaries for writing to the store, including references from
        frontend to backend and vice versa.

        @return A pair of (backpath, frontpath).  backDetails and frontDetails
        will have been updated appropriately, also.

        @param config The configuration of the device, as given to
        {@link #createDevice}.
        @param devid        As returned by {@link #getDeviceDetails}.
        @param backDetails  As returned by {@link #getDeviceDetails}.
        @param frontDetails As returned by {@link #getDeviceDetails}.
        """

        import xen.xend.XendDomain
        xd = xen.xend.XendDomain.instance()

        backdom_name = config.get('backend')
        if backdom_name:
            backdom = xd.domain_lookup_nr(backdom_name)
        else:
            backdom = xd.privilegedDomain()

        if not backdom:
            raise VmError("Cannot configure device for unknown backend %s" %
                          backdom_name)

        frontpath = self.frontendPath(devid)
        backpath  = self.backendPath(backdom, devid)
        
        frontDetails.update({
            'backend' : backpath,
            'backend-id' : "%i" % backdom.getDomid(),
            'state' : str(xenbusState['Initialising'])
            })

        if self.vm.native_protocol:
            frontDetails.update({'protocol' : self.vm.native_protocol})

        backDetails.update({
            'domain' : self.vm.getName(),
            'frontend' : frontpath,
            'frontend-id' : "%i" % self.vm.getDomid(),
            'state' : str(xenbusState['Initialising']),
            'online' : "1"
            })

        return (backpath, frontpath)


    def waitForBackend(self, devid):
        frontpath = self.frontendPath(devid)
        # lookup a phantom
        phantomPath = xstransact.Read(frontpath, 'phantom_vbd')
        if phantomPath is not None:
            log.debug("Waiting for %s's phantom %s.", devid, phantomPath)
            statusPath = phantomPath + '/' + HOTPLUG_STATUS_NODE
            ev = Event()
            result = { 'status': Timeout }
            xswatch(statusPath, hotplugStatusCallback, ev, result)
            ev.wait(DEVICE_CREATE_TIMEOUT)
            err = xstransact.Read(statusPath, HOTPLUG_ERROR_NODE)
            if result['status'] != Connected:
                return (result['status'], err)
            
        backpath = self.readVm(devid, "backend")

        if backpath:
            statusPath = backpath + '/' + HOTPLUG_STATUS_NODE
            ev = Event()
            result = { 'status': Timeout }

            xswatch(statusPath, hotplugStatusCallback, ev, result)

            ev.wait(DEVICE_CREATE_TIMEOUT)

            err = xstransact.Read(backpath, HOTPLUG_ERROR_NODE)

            return (result['status'], err)
        else:
            return (Missing, None)


    def waitForBackend_destroy(self, backpath):

        statusPath = backpath + '/' + HOTPLUG_STATUS_NODE
        ev = Event()
        result = { 'status': Timeout }

        xswatch(statusPath, deviceDestroyCallback, ev, result)

        ev.wait(DEVICE_DESTROY_TIMEOUT)

        return result['status']

    def waitForBackend_reconfigure(self, devid):
        frontpath = self.frontendPath(devid)
        backpath = xstransact.Read(frontpath, "backend")
        if backpath:
            statusPath = backpath + '/' + "state"
            ev = Event()
            result = { 'status': Timeout }

            xswatch(statusPath, xenbusStatusCallback, ev, result)

            ev.wait(DEVICE_CREATE_TIMEOUT)

            return (result['status'], None)
        else:
            return (Missing, None)


    def backendPath(self, backdom, devid):
        """Construct backend path given the backend domain and device id.

        @param backdom [XendDomainInfo] The backend domain info."""

        return "%s/backend/%s/%s/%d" % (backdom.getDomainPath(),
                                        self.deviceClass,
                                        self.vm.getDomid(), devid)


    def frontendPath(self, devid):
        return "%s/%d" % (self.frontendRoot(), devid)


    def frontendRoot(self):
        return "%s/device/%s" % (self.vm.getDomainPath(), self.deviceClass)

    def frontendMiscPath(self):
        return "%s/device-misc/%s" % (self.vm.getDomainPath(),
                                      self.deviceClass)

    def deviceRoot(self):
        """Return the /vm/device. Because backendRoot assumes the
        backend domain is 0"""
        return "%s/device/%s" % (self.vm.vmpath, self.deviceClass)

    def devicePath(self, devid):
        """Return the /device entry of the given VM. We use it to store
        backend/frontend locations"""
        return "%s/device/%s/%s" % (self.vm.vmpath,
                                    self.deviceClass, devid)

def hotplugStatusCallback(statusPath, ev, result):
    log.debug("hotplugStatusCallback %s.", statusPath)

    status = xstransact.Read(statusPath)

    if status is not None:
        if status == HOTPLUG_STATUS_ERROR:
            result['status'] = Error
        elif status == HOTPLUG_STATUS_BUSY:
            result['status'] = Busy
        else:
            result['status'] = Connected
    else:
        return 1

    log.debug("hotplugStatusCallback %d.", result['status'])

    ev.set()
    return 0


def deviceDestroyCallback(statusPath, ev, result):
    log.debug("deviceDestroyCallback %s.", statusPath)

    status = xstransact.Read(statusPath)

    if status is None:
        result['status'] = Disconnected
    else:
        return 1

    log.debug("deviceDestroyCallback %d.", result['status'])

    ev.set()
    return 0


def xenbusStatusCallback(statusPath, ev, result):
    log.debug("xenbusStatusCallback %s.", statusPath)

    status = xstransact.Read(statusPath)

    if status == str(xenbusState['Connected']):
        result['status'] = Connected
    else:
        return 1

    log.debug("xenbusStatusCallback %d.", result['status'])

    ev.set()
    return 0
