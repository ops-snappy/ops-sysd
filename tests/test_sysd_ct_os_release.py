#!/usr/bin/python
#
# (c) Copyright 2016 Hewlett Packard Enterprise Development LP
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

import json
import time
import shutil
import os.path

from mininet.net import Mininet
from mininet.node import Host
from mininet.topo import SingleSwitchTopo
from opsvsi.opsvsitest import info
from opsvsi.opsvsitest import OpsVsiTest
from opsvsi.opsvsitest import OpsVsiLink
from opsvsi.opsvsitest import VsiOpenSwitch


OVS_VSCTL = "/usr/bin/ovs-vsctl "
OVS_APPCTL = "/usr/bin/ovs-appctl "
OVSDB_TOOL = "/usr/bin/ovsdb-tool "

DEFAULT_OS_RELEASE_FILE = "os-release.default"
OS_RELEASE_FILES_DIR = "files/os_releases"


class OSReleaseTest(OpsVsiTest):
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
        """
        Stop sysd & ovsdb-server to prepare for the /etc/os-release change.
        """
        self.__stop()

    def teardown(self):
        """Revert back to the original /etc/os-release file."""
        self.__stop()
        self.__copy_os_release_file()
        self.__start()

    def check_os_name(self, file_name):
        """
        Testing ops-sysd correctly stores switch_info:os_name value.

        Test if the ops-sysd correctly parse the os-release file and stores
        the information in the OVSDB.
        """
        self.__copy_os_release_file(file_name)
        # Restart the ovsdb-server and sysd
        self.__start()

        expected = self.__read_os_release_file(file_name, key='NAME')
        result = self.__get_software_info(key='os_name')

        assert result == expected, "OS name mismatch."

    def check_switch_version(self, file_name):
        """
        Testing ops-sysd correctly stores switch_version column.

        Test if the ops-sysd correctly parse the os-release file and stores
        the information in the OVSDB.
        """
        self.__copy_os_release_file(file_name)
        # Restart the ovsdb-server and sysd
        self.__start()

        version_id = self.__read_os_release_file(file_name, 'VERSION_ID')
        build_id = self.__read_os_release_file(file_name, 'BUILD_ID')
        expected = "{0} (Build: {1})".format(version_id, build_id)
        result = self.__get_switch_version()

        assert result == expected, "Switch version mismatch."

    def __get_software_info(self, key=None):
        out = self.s1.cmd(OVS_VSCTL + "--format json list system")
        data = json.loads(out)['data']
        i = self.__get_system_column_id('software_info')
        if key:
            for (k, v) in data[0][i][1]:
                if k == key:
                    return v
        else:
            return data[0][i]

    def __get_switch_version(self):
        out = self.s1.cmd(OVS_VSCTL + "--format json list system")
        data = json.loads(out)['data']
        i = self.__get_system_column_id('switch_version')
        return data[0][i]

    def __get_system_column_id(self, column_name):
        out = self.s1.cmd(OVS_VSCTL + "--format json list system")
        columns = json.loads(out)['headings']
        i = 0
        for column in columns:
            if column == column_name:
                return i
            i += 1
        return None

    def __read_os_release_file(self, fname=DEFAULT_OS_RELEASE_FILE, key=None):
        """Read the local os-release file and return the data."""
        cur_dir, f = os.path.split(__file__)
        path = os.path.join(cur_dir, OS_RELEASE_FILES_DIR, fname)
        d = {}
        with open(path) as f:
            for line in f:
                k, v = line.rstrip().split("=")
                d[k] = v
        if key:
            return d[key]
        else:
            return d

    def __copy_os_release_file(self, fname=DEFAULT_OS_RELEASE_FILE):
        """Copy a given os-release file to /etc/os-release."""
        src = os.path.join(os.path.sep, 'shared', OS_RELEASE_FILES_DIR, fname)
        dst = os.path.join(os.path.sep, 'etc', 'os-release')
        self.s1.cmd("/bin/cp " + src + " " + dst)

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
        # Create the Mininet topology based on mininet.
        cls.test = OSReleaseTest()

        info("Copying os-release files to switch shared directory.\n")
        cur_dir, f = os.path.split(__file__)
        src = os.path.join(cur_dir, OS_RELEASE_FILES_DIR)
        dst = os.path.join(cls.test.s1.shareddir, OS_RELEASE_FILES_DIR)
        shutil.copytree(src, dst)

    @classmethod
    def teardown_class(cls):
        # Stop the Docker containers, and
        # mininet topology
        cls.test.net.stop()

    def setup(self):
        self.test.setup()

    def teardown(self):
        self.test.teardown()

    def __del__(self):
        del self.test

    def test_ops_default_os_name(self):
        self.test.check_os_name("os-release.default")

    def test_ops_default_version(self):
        self.test.check_switch_version("os-release.default")

    def test_ops_1_0_0_version(self):
        self.test.check_switch_version("os-release.ops-1.0.0")

    def test_debian_8_0_name(self):
        self.test.check_os_name("os-release.debian-8.0")

    def test_debian_8_0_version(self):
        self.test.check_switch_version("os-release.debian-8.0")

    def test_long_name(self):
        self.test.check_os_name("os-release.long-name-and-build-id")

    def test_long_version(self):
        self.test.check_switch_version("os-release.long-name-and-build-id")
