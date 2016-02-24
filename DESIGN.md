# High level design of ops-sysd
The ops-sysd repository contains the source code for the OpenSwitch System Daemon (sysd).

## Responsibilities
sysd is a platform daemon whose primary responsibility is to locate platform specific information and push this information into the OpenSwitch database. sysd starts at boot time along with all of the other OpenSwitch daemons,   locates all of the requisite information, pushes the information to the OpenSwitch database, and then waits for all of the hardware daemons to complete their intialization. At this point, sysd indicates that hardware initialization is complete (through an indicator in the database) and waits in an OVS IDL loop for `appctl` commands.

### image.manifest file
The `/etc/openswitch/image.manifest` file contains platform specific information (produced by the build). This information includes which hardware daemons must complete processing platform hardware information collection and initialization prior to completing the boot sequence.

sysd reads the `image.manifest` file and pushes the daemon information into the openswitch database in the daemon table.

### Daemon information
The hardware daemon information from the `image.manifest` file is written to the daemon table. The **name**, **cur_hw**, and **is_hw_handler** columns are set by sysd. The **cur_hw** column is initialized to zero and hardware daemons set **cur_hw** to one when installation is complete.

### OCP FRU EEPROM
OpenSwitch supports [Open Compute Project (OCP)](http://www.opencompute.org/projects/networking/) compliant switch platforms. OCP compliant platforms include a FRU EEPROM with defined content and format. Using the [config-yaml library](http://git.openswitch.net/cgit/openswitch/ops-config-yaml/tree/README.md), sysd reads the FRU EEPROM content and pushes the information to the base subsystem **other_info** column in the subsystem table.

### Link to hardware description files
sysd creates a symbolic link at `/etc/openswitch/hwdesc` to the directory containing the hardware description files. The build process passes the correct directory location to sysd for the platform specified as the build target.

### System information
sysd manages the system table columns **cur_hw** and **next_hw**. These fields are initially set to zero. sysd monitors the daemon table rows for the hardware daemons (as specified in the `image.manifest` file) and looks to see when all of the daemons have marked their daemon table row **cur_hw** column to one, indicating they have completed their hardware initialization processing. Once all hardware daemons have completed their initialization, sysd sets both **cur_hw** and **next_hw** to a value of one. This informs [Configuration Daemon (cfgd)](http://www.openswitch.net/documents/dev/ops-cfgd/DESIGN) that all hardware initialization is complete and it may proceed to push any saved user configuration into the OpenSwitch database.

### Subsystem information
sysd reads the hardware description file content and extracts subsystem specific information. The **subsystem:other_info** column is populated with the FRU EEPROM information (mentioned above), **interface_count**, **max_interface_speed**, **max_transimission_unit**, **max_bond_count**, **max_bond_member_count**, and **l3_port_requires_interval_vlan**. sysd also sets the values for the interface table pointers in the **interfaces** column and the following subsystem columns:
- name
- asset_tag
- hw_desc_dir
- next_mac_address
- macs_remaining

### Interface information
sysd reads the hardware description file content and extracts the interface specific information. A row is added for each interface. Please see [Interfaces](http:/www.openswitch.net/documents/dev/interfaces_design) for further details, including a discussion on split interfaces.

The following columns are updated in each interface row:
- names
- type
- admin_state
- hw_intf_info
- split_children
- split_parent

### Bridge, port, and vrf information
A default bridge (**bridge_normal**) is created for L2 ports.

A default internal port (**bridge_normal**) and interface (**bridge_normal**) are created for L3 routing across VLANs.

A default VRF (**vrf_default**) is created for L3 ports.

## Design choices
sysd is designed to manage the insertion and removal of hardware information for subsystems. Each platform has at least one subsystem. The primary or constant (always present) hardware for the platform is given the name **base subsystem**. Every platform has exactly one **base subsystem**.

For a top-of-rack switch, also called a pizza box platform, there are typically no dynamically pluggable modules (transceivers are not considered a subsystem). Consequently, you would have only one subsystem with the default name **base**.

Chassis platforms have slots for modules that may be dynamically added or removed. Each such module is considered a subsystem. For chassis platforms, sysd responds to the insertion and removal events of modules and updates the OpenSwitch database for that corresponding subsystem.

For the first release of OpenSwitch, sysd supports only the **base subsystem** and does not support dynamic module (subsystem) insertion and removal.

## Relationships to external OpenSwitch entities<!--Need a good image here-->
```
  +-----------------------+
  |  image.manifest file  |                                      +---------------------+
  +-----------------------+     +--------------------------+     |                     |
              ^                 |   OpenSwitch database    |     |   hardware daemons  |
              |                 |                          |<----+                     |
  +-----------+-----------+     |   system table           |     +---------------------+
  |                       |     |   base subsystem table   |
  |         sysd          +---->|   vrf table              |     +---------------------+
  |                       |     |   interface table        |     |                     |
  +-----------+-----------+     |   daemon table           |<----+   protocol daemons  |
              |                 |   bridge table           |     |                     |
              v                 |   port table             |     +---------------------+
  +-----------------------+     +--------------------------+
  | hw description files  |
  +-----------------------+
```

## OVSDB-Schema
The following OpenSwitch database schema elements are referenced or set by sysd:
```
system table
  system:software_info:os_name
      ->set to the OS name provided by /etc/os-release file.
  system:switch_version
      ->set to the switch version provided by /etc/os-release file.
  system:cur_hw
      ->set to "1" when all hardware daemons have completed initialization
  system:next_hw
      ->set to "1" when all hardware daemons have completed initialization
  system:subsystems
      ->pointers to rows in the subsystem table
  system:daemons
      ->pointers to rows in the daemon table
  system:bridges
      ->pointers to rows in the bridge table
  system:vrfs
      ->pointers to rows in the vrf table
  system:mgmt_intf
      ->Linux interface name for the management interface, such as "eth0"
  system:management_mac
      ->MAC for the management interface
  system:system_mac
      ->MAC for use by any interface that does not require a unique MAC

subsystem table
  subsystem[base]:name
      ->name for this subsystem row
  subsystem[base]:asset_tag
      ->platform asset tag
  subsystem[base]:hw_desc_dir
      ->Linux path to the hardware description files
  subsystem[base]:next_mac_address
      ->Next unused MAC from the MAC pool for this platform
  subsystem[base]:macs_remaining
      ->Number of unused MACs from the MAC pool for this platform
  subsystem[base]:interfaces
      ->pointers to rows in the interface table
  subsystem[base]:other_info:
      ->OCP FRU EEPROM information
  subsystem[base]:other_info:interface_count
      ->number of interfaces for this subsystem
  subsystem[base]:other_info:max_interface_speed
      ->maximum possible interface speed for this subsystem
  subsystem[base]:other_info:max_transimission_unit
      ->maximum allowed MTU setting for this subsystem
  subsystem[base]:other_info:max_bond_count
      ->maximum allowed number of bonds for this subsystem
  subsystem[base]:other_info:max_bond_member_count
      ->maximum allowed members in a bond for this subsystem
  subsystem[base]:other_info:l3_port_requires_interval_vlan
      ->true (1) if L3 ports require an internal VLAN for this subsystem, else false(0)

daemon table
  daemon:name
      ->name for this name row
  daemon:cur_hw
      ->set to 1 by a hardware daemon when it has finished initialization
  daemon:is_hw_handler
      ->set to 1 if this daemon is a hardware daemon that must set cur_hw when done

interface table
  interface:name
      ->name for this interface row
  interface:type
      ->"internal" if for internal use, else "system" if user configurable interface
  interface:admin_state
      ->initialized to "down"
  interface:hw_intf_info
      ->interface specific information from the hardware description files
  interface:split_children
      ->pointer to children interfaces if this is a parent interface
  interface:split_parent
      ->pointer to the parent interface if this is a child interface

bridge table
  bridge:name
      ->name for this bridge row
  bridge:ports
      ->pointers to rows in the port table

port table
  port:name
      ->name for this port row
  port:interfaces
      ->pointers to rows in the interface table

vrf table
  vrf:name
      ->name for this vrf row
```

## Internal structure
### Main loop
Main loop pseudo-code
```
  initialize ovs IDL
  read image.manifest file and process
  locate hardware description files
  create filesystem link to correct set of hardware description files
  initialize config-yaml library
  extract platform information from OCP FRU EEPROM
  extract hardware information from the hardware description files
  while not terminating
    if hardware information not previously pushed
       push hardware information to the db
    if h/w daemons not previously finished initialization
       if now finished
          set hardware daemons done to true in the db
    wait for appctl request or ovs changes
```

### Source modules <!--Need a good image here-->
```
  +----------+
  |  sysd.c  |
  |          +------------------------------+
  |          |appctl interface: responds to |
  |          |ovs-appctl requests           |
  | main     +------------------------------+
  | loop     |
  |          +-----------------------------+----------------------+
  |          |sysd_cfg_yaml.c: Makes calls |  config_yaml library |
  |          |into the config_yaml library |                      |      +------------+
  |          +-----------------------------+                      +----->| HW Desc    |
  |          |                             |                      |      | Files      |
  |          +-----------------------------+                      |      +------------+
  |          |sysd_fru.c: Collects FRU     |             +--------+      +------------+
  |          |EEPROM info via config-yaml  |             | i2c    +----->| FRU EEPROM |
  |          |i2C                          |             |        |      |            |
  |          +-----------------------------+-------------+--------+      +------------+
  |          |
  |          +-----------------------------+      +-------------+
  |          |sysd_ovsdb_if.c: Makes IDL   +----->| OpenSwitch  |
  |          |calls to access database     |      | Database    |
  |          +-----------------------------+      +-------------+
  |          |
  |          +-----------------------------+
  |          |sysd_util.c: Internal        |
  |          |functions                    |
  |          +-----------------------------+
  |          |
  |          +-----------------------------+
  |          |sysd_stub_x86_fru.c: Stub for|
  |          |FRU data in VSI              |
  |          +-----------------------------+
  |          |
  +----------+
```

### Data structures
#### subsystem_t
The primary data structure for sysd is the subsystems structure, which is an array of pointers. A new structure is allocated for each subsystem. Note: For first release, only the **base subsystem** is supported.  The subsystems structure is populated with the information from the hardware description files and is eventually pushed to the subsystem table.

#### daemon_info_t
daemons is an array of pointers to type **daemon_info_t**. This array holds the daemons identified in the `image.manifest` file that are specified as hardware daemons and is pushed to the daemon table.

#### fru_eeprom_t
The OCP FRU EEPROM information is read from the FRU EEPROM and stored in this structure and is later pushed to the subsystem table.

#### sysd_intf_cmn_info_t
The **sysd_intf_cmn_info_t** data structure stores the interface information that is common for all interfaces in the subsystem and is later pushed to the subsytem table.

#### sysd_intf_info_t
The **sysd_intf_info_t** data structure stores the interface specific information and is later pushed to the interface table.

#### mgmt_intf_info_t
The **mgmt_intf_info_t** data structure stores the information for the management interface for this platform and is later pushed to the system table.

## References
* [Open Compute Project (OCP)](http://www.opencompute.org/projects/networking/)
* [config-yaml library](http://git.openswitch.net/cgit/openswitch/ops-config-yaml/tree/README.md)
* [Configuration Daemon (cfgd)](http://www.openswitch.net/documents/dev/ops-cfgd/DESIGN)
