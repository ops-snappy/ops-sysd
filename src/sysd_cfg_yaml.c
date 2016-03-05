/************************************************************************//**
 * (c) Copyright 2015-2016 Hewlett Packard Enterprise
 * Development LP
 *
 *    Licensed under the Apache License, Version 2.0 (the "License"); you may
 *    not use this file except in compliance with the License. You may obtain
 *    a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 *    License for the specific language governing permissions and limitations
 *    under the License.
 *
 ***************************************************************************/
/* @ingroup sysd
 *
 * @file
 * Source for sysd config_yaml interface.
 *
 */

/* OPS_TODO: The current code assumes that the switch is a simple pizza box.
 *           Will need to refactor to support multiple subsytems. */

#include <stdio.h>
#include <stdlib.h>

#include <openvswitch/vlog.h>

#include <config-yaml.h>
#include "sysd.h"
#include "sysd_cfg_yaml.h"

VLOG_DEFINE_THIS_MODULE(cfg_yaml);

/** @ingroup sysd
 * @{ */

#define FRU_EEPROM_NAME "fru_eeprom"

static YamlConfigHandle cfg_yaml_handle = (YamlConfigHandle *)NULL;
static const YamlDevice *fru_dev = NULL;


bool
sysd_cfg_yaml_open(char *hw_desc_dir)
{
    int rc = 0;

    cfg_yaml_handle = yaml_new_config_handle();

    rc = yaml_add_subsystem(cfg_yaml_handle, BASE_SUBSYSTEM, hw_desc_dir);
    if (rc) {
        VLOG_ERR("Unable to create '%s' subsystem (yaml parsing).", BASE_SUBSYSTEM);
        return(false);
    }

    return(true);
} /* sysd_cfg_yaml_open */

bool
sysd_cfg_yaml_init(char *hw_desc_dir)
{
    int rc = 0;

    if (!sysd_cfg_yaml_open(hw_desc_dir)) {
        return(false);
    }

    rc = yaml_parse_devices(cfg_yaml_handle, BASE_SUBSYSTEM);
    if (0 > rc) {
        VLOG_ERR("Unable to parse devices yaml config file.");
        return (false);
    }

    rc = yaml_parse_ports(cfg_yaml_handle, BASE_SUBSYSTEM);
    if (0 > rc) {
        VLOG_ERR("Unable to parse ports yaml config file.");
        return (false);
    }

    rc = yaml_parse_qos(cfg_yaml_handle, BASE_SUBSYSTEM);
    if (0 > rc) {
        VLOG_ERR("Unable to parse qos yaml config file.");
        return (false);
    }

    rc = yaml_init_devices(cfg_yaml_handle, BASE_SUBSYSTEM);
    if (0 > rc) {
        VLOG_ERR("Failed to intialize devices");
        return (false);
    }

    fru_dev = yaml_find_device(cfg_yaml_handle, BASE_SUBSYSTEM, FRU_EEPROM_NAME);
    if (fru_dev == (YamlDevice *)NULL) {
        VLOG_ERR("unable to find device %s in YAML description.", FRU_EEPROM_NAME);
        return (false);
    }

#ifdef NOTYET
    rc = yaml_init_qos(cfg_yaml_handle, BASE_SUBSYSTEM);
    if (0 > rc) {
        VLOG_ERR("Failed to intialize qos");
        return (false);
    }
#endif

    return (true);

} /* sysd_cfg_yaml_init */

int
sysd_cfg_yaml_get_port_count(void)
{
    return (int) yaml_get_port_count(cfg_yaml_handle, BASE_SUBSYSTEM);

} /* sysd_cfg_yaml_get_port_count */

YamlPort *
sysd_cfg_yaml_get_port_info(int index)
{
    return (YamlPort *) yaml_get_port(cfg_yaml_handle, BASE_SUBSYSTEM, index);

} /* sysd_cfg_yaml_get_port_info */

YamlPortInfo *
sysd_cfg_yaml_get_port_subsys_info(void)
{
    return yaml_get_port_info(cfg_yaml_handle, BASE_SUBSYSTEM);

} /* sysd_cfg_yaml_get_port_subsys_info */

bool
sysd_cfg_yaml_fru_read(unsigned char *fru_hdr, int hdr_len)
{
    int         rc;
    i2c_op      op;
    i2c_op      *cmds[2];

    op.direction        = READ;
    op.device           = fru_dev->name;
    op.register_address = 0;
    op.byte_count       = hdr_len;
    op.data             = fru_hdr;
    op.set_register     = false;
    op.negative_polarity = false;

    cmds[0] = &op;
    cmds[1] = (i2c_op *) NULL;

    rc = i2c_execute(cfg_yaml_handle, BASE_SUBSYSTEM, fru_dev, cmds);
    if (0 != rc) {
        VLOG_ERR("Failed to read FRU header.");
        return (false);
    }

    return (true);

} /* sysd_cfg_yaml_fru_read */

YamlQosInfo *
sysd_cfg_yaml_get_qos_info(void)
{
    return yaml_get_qos_info(cfg_yaml_handle, BASE_SUBSYSTEM);
}

int
sysd_cfg_yaml_get_cos_map_entry_count(void)
{
    return yaml_get_cos_map_entry_count(cfg_yaml_handle, BASE_SUBSYSTEM);
}

const YamlCosMapEntry *
sysd_cfg_yaml_get_cos_map_entry(unsigned int idx)
{
    return yaml_get_cos_map_entry(cfg_yaml_handle, BASE_SUBSYSTEM, idx);
}

int
sysd_cfg_yaml_get_dscp_map_entry_count(void)
{
    return yaml_get_dscp_map_entry_count(cfg_yaml_handle, BASE_SUBSYSTEM);
}

const YamlDscpMapEntry *
sysd_cfg_yaml_get_dscp_map_entry(unsigned int idx)
{
    return yaml_get_dscp_map_entry(cfg_yaml_handle, BASE_SUBSYSTEM, idx);
}

int
sysd_cfg_yaml_get_schedule_profile_entry_count(void)
{
    return yaml_get_schedule_profile_entry_count(cfg_yaml_handle, BASE_SUBSYSTEM);
}

const YamlScheduleProfileEntry *
sysd_cfg_yaml_get_schedule_profile_entry(unsigned int idx)
{
    return yaml_get_schedule_profile_entry(cfg_yaml_handle, BASE_SUBSYSTEM, idx);
}

int
sysd_cfg_yaml_get_queue_profile_entry_count(void)
{
    return yaml_get_queue_profile_entry_count(cfg_yaml_handle, BASE_SUBSYSTEM);
}

const YamlQueueProfileEntry *
sysd_cfg_yaml_get_queue_profile_entry(unsigned int idx)
{
    return yaml_get_queue_profile_entry(cfg_yaml_handle, BASE_SUBSYSTEM, idx);
}

/** @} end of group sysd */
