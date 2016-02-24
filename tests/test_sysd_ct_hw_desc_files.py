#!/usr/bin/python
#
# (c) Copyright 2015-2016 Hewlett Packard Enterprise Development LP
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

import time
import yaml
import shutil
import os.path

from mininet.net import Mininet
from mininet.node import Host
from mininet.topo import SingleSwitchTopo
from opsvsi.opsvsitest import info
from opsvsi.opsvsitest import debug
from opsvsi.opsvsitest import OpsVsiTest
from opsvsi.opsvsitest import OpsVsiLink
from opsvsi.opsvsitest import VsiOpenSwitch


OVS_VSCTL = "/usr/bin/ovs-vsctl "
OVS_APPCTL = "/usr/bin/ovs-appctl "
OVSDB_TOOL = "/usr/bin/ovsdb-tool "

HW_DESC_FILES_DIR = "test_hw_desc_files"
DUT_HW_DESC_FILES_DIR = "/shared/test_hw_desc_files/"
SYSTEM_HW_DESC_FILES_DIR = "/etc/openswitch/hwdesc"


class HWDescTest(OpsVsiTest):
    """Mininet based OpenSwitch component test class.

    This class will be instantiated by the py.test TestRunner below.
    """

    def setupNet(self):
        # Create a topology with single openswitch.
        switch_opts = self.getSwitchOpts()
        intfd_topo = SingleSwitchTopo(k=0, sopts=switch_opts)
        self.net = Mininet(intfd_topo, switch=VsiOpenSwitch,
                           host=Host, link=OpsVsiLink,
                           controller=None, build=True)
        self.s1 = self.net.switches[0]

    def setup(self):
        # Stop sysd & ovsdb-server (unload the database also).
        self.__stop()

        # Copy new hardware description files.
        self.__copy_hw_desc_files()

        # Restart ovsdb-server and sysd.
        self.__start()

        # Prepare the expected values from the 'ports.yaml' file.
        self.expected = self.__read_yaml_port_file("ports.yaml")

    def teardown(self):
        pass

    def check_number_of_ports(self):
        count = self.__get_subsystem_other_info("interface_count")
        expected = str(self.expected['port_info']['number_ports'])
        assert count == expected, "Number of ports in OVSDB is incorrect."

    def check_max_bond_count(self):
        count = self.__get_subsystem_other_info("max_bond_count")
        expected = str(self.expected['port_info']['max_lag_count'])
        assert count == expected, "Max number of lags in DB is incorrect."

    def check_max_bond_member_count(self):
        count = self.__get_subsystem_other_info("max_bond_member_count")
        expected = str(self.expected['port_info']['max_lag_member_count'])
        assert count == expected, "Max number of members in a lag in DB " \
                                  "is incorrect."

    def check_switch_intf_id(self):
        # Match the port hw_intf_info details.
        port_name = self.expected['ports'][0]['name']
        intf_id = self.__get_interface_hw_info(port_name, "switch_intf_id")
        expected = str(self.expected['ports'][0]['switch_device_port'])
        assert intf_id == expected, "Interface hardware info:switch_intf_id " \
                                    "is incorrect."

    def check_connector_type(self):
        port_name = self.expected['ports'][0]['name']
        connector_type = self.__get_interface_hw_info(port_name, "connector")
        expected = str(self.expected['ports'][0]['connector'])
        assert connector_type == expected, "Interface hardware " \
                                           "info:connector is incorrect."

    def check_default_bridge_name(self):
        name = self.s1.ovscmd(OVS_VSCTL + "get bridge bridge_normal name")
        name = name.replace('\r\n', '')
        expected = "bridge_normal"
        assert name == expected, "Default bridge is not created."

    def check_default_vrf_name(self):
        name = self.s1.ovscmd(OVS_VSCTL + "get vrf vrf_default name")
        name = name.replace('\r\n', '')
        expected = "vrf_default"
        assert name == expected, "Default VRF is not created."

    def __start(self):
        self.__start_ovsdb()
        self.__sleep(3)
        self.__start_sysd()
        self.__wait_until_ovsdb_is_up()

    def __stop(self):
        self.__stop_sysd()
        self.__stop_ovsdb()
        self.__sleep(3)

    def __start_sysd(self):
        self.s1.cmd("/bin/systemctl start ops-sysd")

    def __stop_sysd(self):
        self.s1.cmd(OVS_APPCTL + "-t ops-sysd exit")

    def __start_ovsdb(self):
        """Create an empty DB file and load it into ovsdb-server."""

        # Create an empty database file.
        c = OVSDB_TOOL + "create /var/run/openvswitch/ovsdb.db " \
                         "/usr/share/openvswitch/vswitch.ovsschema"
        self.s1.cmd(c)

        # Load the newly created DB into ovsdb-server
        self.s1.cmd(OVS_APPCTL + "-t ovsdb-server ovsdb-server/add-db "
                    "/var/run/openvswitch/ovsdb.db")

    def __stop_ovsdb(self):
        """Remove the OpenSwitch DB from ovsdb-server.

        It also removes the DB file from the file system.
        """

        # Remove the database from the ovsdb-server.
        self.s1.cmd(OVS_APPCTL +
                    "-t ovsdb-server ovsdb-server/remove-db OpenSwitch")

        # Remove the DB file from the file system.
        self.s1.cmd("/bin/rm -f /var/run/openvswitch/ovsdb.db")

    def __copy_hw_desc_files(self):
        """Copy Hardware description files to /etc/openswitch/hwdesc"""
        c = "/bin/cp -a " + DUT_HW_DESC_FILES_DIR + "/*.yaml " + \
            SYSTEM_HW_DESC_FILES_DIR
        self.s1.cmd(c)

    def __read_yaml_port_file(self, file_name):
        """Read the local port.yaml file and return the data."""
        cur_dir, f = os.path.split(__file__)
        yaml_file_loc = cur_dir + "/" + HW_DESC_FILES_DIR + "/" + file_name
        debug("Loading the image manifest file:" + yaml_file_loc + "\n")

        yaml_data = open(yaml_file_loc, "r")
        return yaml.load(yaml_data)

    def __get_subsystem_other_info(self, map_key):
        """Get the value from subsystem table other_info column."""
        subsystem_uuid = self.s1.ovscmd(OVS_VSCTL + "list subsystem |"
                                        "grep -i uuid|cut -d : -f 2")
        uuid = subsystem_uuid.replace('\r\n', '')
        c = OVS_VSCTL + "get subsystem " + uuid + " other_info:" + map_key
        out = self.s1.ovscmd(c)
        return out.replace('\r\n', '')

    def __get_interface_hw_info(self, name, map_key):
        """Get the value from interface table hw_intf_info column."""
        c = OVS_VSCTL + "get interface " + str(name) + \
            " hw_intf_info:" + map_key
        out = self.s1.ovscmd(c)
        return out.replace('\r\n', '')

    def __wait_until_ovsdb_is_up(self):
        """Wait until System table is visible in the ovsdb-server."""
        cmd = OVS_VSCTL + "list System | grep uuid"
        wait_count = 20
        while wait_count > 0:
            out = self.s1.ovscmd(cmd)
            if "_uuid" in out:
                break

            info(out)
            wait_count -= 1
            self.__sleep(1)
        assert wait_count != 0, "Failed to bring up ovsdb-server."

    def __sleep(self, tm=.5):
        time.sleep(tm)


class TestRunner:
    """py.test based test runner class."""
    @classmethod
    def setup_class(cls):
        """Create the Mininet topology based on mininet."""
        cls.test = HWDescTest()

        # Copy the test files (image.manifest, hw_descr files)
        # from current directory to switch shared directory.
        cur_dir, f = os.path.split(__file__)

        info("Copying test hw description file to switch shared directory.\n")
        test_file_dir = os.path.join(cur_dir, HW_DESC_FILES_DIR)
        shutil.copytree(test_file_dir, cls.test.s1.shareddir + "/" +
                        HW_DESC_FILES_DIR)

    @classmethod
    def teardown_class(cls):
        """Stop the Docker containers, and mininet topology"""
        cls.test.net.stop()

    def setup(self):
        self.test.setup()

    def teardown(self):
        self.test.teardown()

    def __del__(self):
        del self.test

    # sysd h/w component testing.
    def test_number_of_ports(self):
        self.test.check_number_of_ports()

    def test_max_bond_count(self):
        self.test.check_max_bond_count()

    def test_max_bond_member_count(self):
        self.test.check_max_bond_member_count()

    def test_switch_intf_id(self):
        self.test.check_switch_intf_id()

    def test_connector_type(self):
        self.test.check_connector_type()

    def test_default_bridge_name(self):
        self.test.check_default_bridge_name()

    def test_default_vrf_name(self):
        self.test.check_default_vrf_name()
