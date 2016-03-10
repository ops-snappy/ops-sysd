# ops-sysd Test Cases

## Contents
- [Image manifest read test](#image-manifest-read-test)
- [Hardware description file read test](#hardware-description-files-read-test)
- [/etc/os-release file read test](#etcos-release-file-read-test)
- [Package_Info table initialization test](#packageinfo-table-initialization-test)


## Image manifest read test

### Objective
Verify that sysd correctly processes the image.manifest file.

### Requirements
Virtual Mininet Test Setup.

### Setup
#### Topology diagram
```
  [s1]
```

### Description
Bring up ops-sysd with various image.manifest files and verify that
the daemon information in the database matches the information in
the image.manifest files.

### Test result criteria
#### Test pass criteria
This test passes if the ops-sysd daemon:

- Changes the hardware handler field to `false`.
- Changes the management interface from `eth0` to `mgmt1`.
- Behaves correctly even with random information in the file.


#### Test fail criteria
One or more of the verifications fail.


## Hardware description files read test

### Objective
Verify that ops-sysd correctly processes the hardware description files.

### Requirements
Virtual Mininet Test Setup.

### Setup
#### Topology diagram
```
  [s1]
```

### Description
Bring up ops-sysd with various hardware description files and check
if the daemon correctly populates the information in the appropriate
tables or columns in the database.

### Test result criteria
#### Test pass criteria
This test passes if the hardware description files match the values
in the following parameters:

- number\_ports
- max\_bond\_count
- max\_lag\_member\_count
- switch\_device\_port
- connector
- bridge\_normal
- vrf\_default

#### Test fail criteria
This test fails if one of the verifications is unsuccessful. For example:
the number of ports is not correct.


## /etc/os-release file read test

### Objective
Verify that ops-sysd correctly processes the /etc/os-release file.

### Requirements
Virtual Mininet Test Setup.

### Setup
#### Topology diagram
```
  [s1]
```

### Description
1. Copy the sample os-release files to the VSI switch /tmp directory.
2. Stop the OVSDB server as well as ops-sysd on the switch.
3. Copy the specific os-release file, such as os-release.ops-1.0.0,
   to the /etc/os-release file.
4. Start the OVSDB server as well as ops-sysd on the switch.
5. Verify that the software\_info.os\_name as well as
   switch\_version column of the System table in OVSDB shows the
   corresponding information stored in the /etc/os-release file.

### Test result criteria
#### Test pass criteria
- Verify that the OS name in the OVSDB matches with the appropriate
  /etc/os-release NAME entry.
- Verify that the switch version in the OVSDB matches the appropriate
  /etc/os-release VERSION\_ID and BUILD\_ID entries.

#### Test fail criteria
Verify that the switch version in the OVSDB matches the appropriate
/etc/os-release NAME, VERSION\_ID, and BUILD\_ID.

## Package_Info table initialization test

### Objective
Verify that the Package_Info table is populated during the sysd initialization phase.

### Requirements
Virtual Mininet Test Setup.

### Setup
#### Topology diagram
```
  [s1]
```

### Description
1. Read the contents of the Package_Info table from OVSDB.
2. Verify if `ops-sysd` can be found in the Package_Info table.
   (An entry for `ops-sysd` is present in the Package_Info table
   if it was correctly populated during sysd initialization.)

### Test result criteria
#### Test pass criteria
The `ops-sysd` entry was found in the Package_Info table.

#### Test fail criteria
The `ops-sysd` entry was not found in the Package_Info table.
