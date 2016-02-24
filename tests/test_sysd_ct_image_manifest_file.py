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

import json
import time
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

IMAGE_MANIFEST_FILES_DIR = "test_manifest_files"
DUT_IMAGE_MANIFEST_FILES_DIR = "/shared/test_manifest_files/"
SYSTEM_IMAGE_MANIFEST_FILE = "/etc/openswitch/image.manifest"


class ImageManifestTest(OpsVsiTest):
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
        """Stop sysd & ovsdb-server to prepare for the manifest change."""
        self.__stop()

    def teardown(self):
        """Revert back to the original manifest file."""
        self.__stop()
        self.__copy_image_manifest_file("image.manifest")
        self.__start()

    def image_manifest_read(self, file_name):
        """Testing ops-sysd correctly parse image manifest file

        Test if the ops-sysd correctly parse the image manifest file and stores
        the information in the OVSDB.
        """
        # Copy a new image.manifest file to the switch
        self.__copy_image_manifest_file(file_name)

        # Restart the ovsdb-server and sysd
        self.__start()

        # Make sure that the data in ovsdb-server matches the local file.
        ovsdb_daemons_list = self.__list_daemons()
        file_daemons_list = self.__read_image_manifest_file(file_name)
        ret = cmp(ovsdb_daemons_list, file_daemons_list)
        assert ret == 0, "incorrect image.manifest info."

    def __list_daemons(self):
        """Get daemon table from ovsdb-server."""
        daemon_list = {}
        c = OVS_VSCTL + "--format json list daemon"
        out = self.s1.cmd(c)
        json_out = json.loads(out)['data']

        # The output is in the following format
        # [["uuid","19b943b0-096c-4d7c-bc0c-5b6ac2f83014"],0,true,"ops-pmd"]
        for item in json_out:
            daemon_list[item[3]] = {'is_hw_handler': item[2]}

        return daemon_list

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

    def __copy_image_manifest_file(self, file_name):
        """
        Copy a given image.manifest file to /etc/openswitch/image.manifest
        """
        c = "/bin/cp " + DUT_IMAGE_MANIFEST_FILES_DIR + file_name + \
            " " + SYSTEM_IMAGE_MANIFEST_FILE
        self.s1.cmd(c)

    def __read_image_manifest_file(self, file_name):
        """Read the local image.manifest file and return the data."""
        cur_dir, f = os.path.split(__file__)
        json_file_loc = (cur_dir + "/" + IMAGE_MANIFEST_FILES_DIR +
                         "/" + file_name)
        debug("Loading the YAML file:" + json_file_loc + "\n")

        json_data = open(json_file_loc).read()
        return json.loads(json_data)['daemons']

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
        cls.test = ImageManifestTest()

        # Copy the test files (image.manifest, hw_descr files)
        # from current directory to switch shared directory.
        cur_dir, f = os.path.split(__file__)

        info("Copying image manifest files to switch shared directory.\n")
        test_file_dir = os.path.join(cur_dir, IMAGE_MANIFEST_FILES_DIR)
        shutil.copytree(test_file_dir, cls.test.s1.shareddir + "/" +
                        IMAGE_MANIFEST_FILES_DIR)

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

    # sysd component tests.
    def test_change_hw_handler_to_false(self):
        """Change the some of the hardware handler to false."""
        self.test.image_manifest_read("image.manifest1")

    def test_change_mgmt_intf_from_eth0_to_mgmt1(self):
        """Change the management interface from eth0 to mgmt1."""
        self.test.image_manifest_read("image.manifest2")

    def test_add_random_stuff(self):
        """Add Random_stuff field."""
        self.test.image_manifest_read("image.manifest3")
