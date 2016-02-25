# (c) Copyright 2015 Hewlett Packard Enterprise Development LP
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
# sysd/CMakeLists.txt

cmake_minimum_required (VERSION 2.8)

set (SYSD ops-sysd)
project (${SYSD})

set (SRC_DIR src)
set (INCL_DIR include)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall -Werror")

OPTION( PLATFORM_SIMULATION "Enable platform simulation" OFF )

set (SYSCONFDIR "/etc" CACHE STRING "Location of system configuration files")
set (HWDESC_FILE_LINK_PATH ${SYSCONFDIR}/openswitch)
set (HWDESC_FILE_LINK ${SYSCONFDIR}/openswitch/hwdesc)
set (HWDESC_FILES_PATH ${SYSCONFDIR}/openswitch/platform)

# Update the sysd.h with any compile time flags
configure_file (${PROJECT_SOURCE_DIR}/${INCL_DIR}/sysd.h.in
                ${PROJECT_BINARY_DIR}/${INCL_DIR}/sysd.h)

# Update the platform YAML config file location in sysd_cfg_yaml
configure_file (${PROJECT_SOURCE_DIR}/${INCL_DIR}/sysd_cfg_yaml.h.in
                ${PROJECT_BINARY_DIR}/${INCL_DIR}/sysd_cfg_yaml.h)

set (MANIFEST_FILE_PATH /etc/openswitch/image.manifest)
set (OS_RELEASE_FILE_PATH /etc/os-release)

# Update the image.manifest file location in sysd_util
configure_file (${PROJECT_SOURCE_DIR}/${INCL_DIR}/sysd_util.h.in
                ${PROJECT_BINARY_DIR}/${INCL_DIR}/sysd_util.h)

# Rules to locate needed libraries
include(FindPkgConfig)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(CONFIG_YAML REQUIRED ops-config-yaml)
pkg_check_modules(OPSUTILS REQUIRED opsutils)
pkg_check_modules(OVSCOMMON REQUIRED libovscommon)
pkg_check_modules(OVSDB REQUIRED libovsdb)

include_directories (${PROJECT_BINARY_DIR}/${INCL_DIR}
                     ${PROJECT_SOURCE_DIR}/${INCL_DIR}
                     ${OVSCOMMON_INCLUDE_DIRS})

# Source files to build ops-sysd
set (SOURCES ${SRC_DIR}/sysd.c
             ${SRC_DIR}/sysd_cfg_yaml.c
             ${SRC_DIR}/sysd_fru.c
             ${SRC_DIR}/sysd_ovsdb_if.c
             ${SRC_DIR}/qos_init.c
             ${SRC_DIR}/sysd_qos_utils.c
             ${SRC_DIR}/sysd_util.c)
if (PLATFORM_SIMULATION)
        LIST(APPEND SOURCES ${SRC_DIR}/sysd_stub_x86_fru.c)
endif ()

# Rules to build ops-sysd
add_executable (${SYSD} ${SOURCES})

target_link_libraries (${SYSD} ${OPSUTILS_LIBRARIES} ${CONFIG_YAML_LIBRARIES}
                       ${OVSCOMMON_LIBRARIES} ${OVSDB_LIBRARIES} ${ZLIB_LIBRARIES}
                       -lpthread -lrt)

# The default install prefix is /usr. We want to install manifest file at
# '/etc/openswitch'. So change the install prefix to '/', and use relative
# directories in the install command.
SET(CMAKE_INSTALL_PREFIX "/" CACHE STRING "Prefix prepended to install directories" FORCE)

# Rules to install ops-sysd binary in rootfs
install(TARGETS ${SYSD}
        RUNTIME DESTINATION usr/bin)

# OPS_TODO: The image.manifest file should not be located in sysd.
# This is just temporary parking space until we find it better home.
install(FILES files/image.manifest
        DESTINATION etc/openswitch)
