# -*- coding: utf-8 -*-
# (C) Copyright 2015 Hewlett Packard Enterprise Development LP
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#
##########################################################################

"""
OpenSwitch Test for checking platform daemons and for errors while bootup.
"""

from time import sleep

from .helpers import wait_until_interface_up

from .platform_err_msgs import err_msgs

TOPOLOGY = """
# +-------+
# |  ops1 |
# +-------+

# Nodes
[type=openswitch name="OpenSwitch 1"] ops1
[type=openswitch name="OpenSwitch 2"] ops2

ops1:if01 -- ops2:if01
"""


ops1_router_id = "10.0.10.1"
ops2_router_id = "10.0.10.2"

network_pl = "24"
ops_router_ids = [ops1_router_id, ops2_router_id]

dutarray = []

hw_daemons = [
    'ops-switchd',
    'ops-sysd',
    'ops-pmd',
    'ops-tempd',
    'ops-powerd',
    'ops-ledd',
    'ops-fand']

msgs = err_msgs()


def configure_switch_ips(step):
    step("Configuring switch IPs...")

    sw1 = dutarray[0]
    sw2 = dutarray[1]

    # Configure IP and bring UP switch 1 interfaces
    with sw1.libs.vtysh.ConfigInterface('1') as ctx:
        ip_addr = ops_router_ids[0] + "/" + network_pl
        ctx.ip_address(ip_addr)
        ctx.no_shutdown()

    # Configure IP and bring UP switch 2 interfaces
    with sw2.libs.vtysh.ConfigInterface('1') as ctx:
        ip_addr = ops_router_ids[1] + "/" + network_pl
        ctx.ip_address(ip_addr)
        ctx.no_shutdown()


def verify_bootup(step):
    step("Verifying platform processes running...")

    switch = dutarray[0]

    dump = switch("ps -e", shell="bash")

    for daemon in hw_daemons:
        if daemon in str(dump):
            step("%s daemon is running" % (daemon))
        else:
            assert False, step("%s daemon is not running" % (daemon))


def verify_bootup_logs(step):
    step("Finding errors while system bootup...")

    switch = dutarray[0]

    switch("cp /var/log/messages /messages", shell="bash")

    for err in msgs:
        sh_cmd = 'cat /messages | grep "' + err + '"'
        cmd_out = switch(sh_cmd, shell="bash")
        lst = cmd_out.split('\n')
        # lst.pop(0)
        for item in lst:
            if err in item:
                assert False, step("There is an error while system bootup")


def verify_ping(step):
    step("Verifying ping request...")

    sw1 = dutarray[0]
    sw2 = dutarray[1]

    # Wait until interfaces are up
    for switch, portlbl in [(sw1, '1'), (sw2, '1')]:
        wait_until_interface_up(switch, portlbl)

    sleep(2)
    # Ping IPv4-address from switch2 to host1
    step("Test ping IPv4-address from sw1 to sw2")
    ping_str = "ping " + ops_router_ids[1]

    dump = sw1(ping_str)

    assert '0% packet loss' in dump, "Ping"
    " between switches failed"


def test_ft_bootup(topology, step):
    ops1 = topology.get("ops1")
    ops2 = topology.get("ops2")
    assert ops1 is not None
    assert ops2 is not None

    global dutarray
    dutarray = [ops1, ops2]

    configure_switch_ips(step)

    verify_bootup(step)
    verify_bootup_logs(step)
    verify_ping(step)
