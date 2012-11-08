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

"""Xend interface to networking control scripts.
"""

import XendOptions
from xen.util import xpopen

def network(op):
    """Call a network control script.

    @param op: operation (start, stop)
    """
    if op not in ['start', 'stop']:
        raise ValueError('Invalid operation: ' + op)
    script = XendOptions.instance().get_network_script()
    if script:
        script.insert(1, op)
        xpopen.call(script)
