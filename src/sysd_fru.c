/************************************************************************//**
 * (c) Copyright 2015 Hewlett Packard Enterprise Development LP
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
 * Source for sysd FRU EEPROM access interface.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <openvswitch/vlog.h>
#include <util.h>

#include <ops-utils.h>
#include <config-yaml.h>
#include <i2c.h>

#include "sysd_util.h"
#include "sysd_fru.h"
#include "sysd_cfg_yaml.h"
#include "sysd.h"

#include "eventlog.h"

VLOG_DEFINE_THIS_MODULE(fru);

/** @ingroup sysd
 * @{ */

bool
sysd_process_eeprom(unsigned char *buf, fru_eeprom_t *fru_eeprom, int len)
{
    int                 idx = 0;
    int                 skip;
    int                 tlv_len = 0;
    int                 crc_len;
    unsigned char       *bp;
    char                *tlv_value = NULL;
    unsigned int        chksum;
    unsigned int        found_crc;
    fru_tlv_t           *fru_tlv;

    bp = buf;

    /* Skip the FRU header. */
    buf += sizeof(fru_header_t);

    while (idx < len) {
        fru_tlv = (fru_tlv_t *) buf;
        tlv_value = (char *) fru_tlv->value;
        tlv_len = (unsigned int) fru_tlv->length;

        switch(fru_tlv->code) {
            case FRU_PRODUCT_NAME_TYPE:
                SYSD_MALLOC(fru_eeprom->product_name, tlv_len + 1);
                strncpy(fru_eeprom->product_name, tlv_value, tlv_len);
                fru_eeprom->product_name[tlv_len] = '\0';
                break;

            case FRU_PART_NUMBER_TYPE:
                SYSD_MALLOC(fru_eeprom->part_number, tlv_len + 1);
                strncpy(fru_eeprom->part_number, tlv_value, tlv_len);
                fru_eeprom->part_number[tlv_len] = '\0';
                break;

            case FRU_SERIAL_NUMBER_TYPE:
                SYSD_MALLOC(fru_eeprom->serial_number, tlv_len + 1);
                strncpy(fru_eeprom->serial_number, tlv_value, tlv_len);
                fru_eeprom->serial_number[tlv_len] = '\0';
                break;

            case FRU_NUM_MAC_TYPE:
                /* Two byte big-endian uint */
                fru_eeprom->num_macs = (uint16_t) (tlv_value[0] << 8) | (tlv_value[1]);
                break;

            case FRU_BASE_MAC_ADDRESS_TYPE:
                bcopy(tlv_value, fru_eeprom->base_mac_address, FRU_BASE_MAC_ADDRESS_LEN);
                break;

            case FRU_MANUFACTURE_DATE_TYPE:
                strncpy(fru_eeprom->manufacture_date, tlv_value, tlv_len + 1);
                fru_eeprom->manufacture_date[tlv_len] = '\0';
                break;

            case FRU_DEVICE_VERSION_TYPE:
                fru_eeprom->device_version = tlv_value[0];
                break;

            case FRU_LABEL_REVISION_TYPE:
                SYSD_MALLOC(fru_eeprom->label_revision, tlv_len + 1);
                strncpy(fru_eeprom->label_revision, tlv_value, tlv_len);
                fru_eeprom->label_revision[tlv_len] = '\0';
                break;

            case FRU_PLATFORM_NAME_TYPE:
                SYSD_MALLOC(fru_eeprom->platform_name, tlv_len + 1);
                strncpy(fru_eeprom->platform_name, tlv_value, tlv_len);
                fru_eeprom->platform_name[tlv_len] = '\0';
                break;

            case FRU_ONIE_VERSION_TYPE:
                SYSD_MALLOC(fru_eeprom->onie_version, tlv_len + 1);
                strncpy(fru_eeprom->onie_version, tlv_value, tlv_len);
                fru_eeprom->onie_version[tlv_len] = '\0';
                break;

            case FRU_MANUFACTURER_TYPE:
                SYSD_MALLOC(fru_eeprom->manufacturer, tlv_len + 1);
                strncpy(fru_eeprom->manufacturer, tlv_value, tlv_len);
                fru_eeprom->manufacturer[tlv_len] = '\0';
                break;

            case FRU_COUNTRY_CODE_TYPE:
                strncpy(fru_eeprom->country_code, tlv_value, tlv_len + 1);
                fru_eeprom->country_code[tlv_len] = '\0';
                break;

            case FRU_VENDOR_TYPE:
                SYSD_MALLOC(fru_eeprom->vendor, tlv_len + 1);
                strncpy(fru_eeprom->vendor, tlv_value, tlv_len);
                fru_eeprom->vendor[tlv_len] = '\0';
                break;

            case FRU_DIAG_VERSION_TYPE:
                SYSD_MALLOC(fru_eeprom->diag_version, tlv_len + 1);
                strncpy(fru_eeprom->diag_version, tlv_value, tlv_len);
                fru_eeprom->diag_version[tlv_len] = '\0';
                break;

            case FRU_SERVICE_TAG_TYPE:
                SYSD_MALLOC(fru_eeprom->service_tag, tlv_len + 1);
                strncpy(fru_eeprom->service_tag, tlv_value, tlv_len);
                fru_eeprom->service_tag[tlv_len] = '\0';
                break;

            case FRU_VENDOR_EXTENSION_TYPE:
                /* OPS_TODO: Currently vendor specific extension TLVs
                 *           are not supported. Ignore them. */
                break;

            case FRU_CRC_TYPE:
                /* CRC-32 */
                crc_len = len + sizeof(fru_header_t) - tlv_len;
                chksum = calc_crc(bp, crc_len);
                VLOG_DBG("calculated crc is 0x%04x", chksum);
                found_crc = ((fru_tlv->value[0] & 0xff) << 24 |
                             (fru_tlv->value[1] & 0xff) << 16 |
                             (fru_tlv->value[2] & 0xff) << 8 |
                             (fru_tlv->value[3] & 0xff));
                if (chksum != found_crc) {
                    VLOG_ERR("Invalid CRC: found 0x%04x calculated 0x%04x", found_crc, chksum);
                    return (false);
                }
                break;

            case 0x00:
            case 0xff:
            default:
                VLOG_ERR("Illegal FRU TLV type 0x%x", fru_tlv->code);
                return (false);
                break;
        }

        skip = fru_tlv->length+sizeof(fru_tlv->length)+sizeof(fru_tlv->code);
        idx += skip;
        buf += skip;
    }

    return (true);
} /* sysd_process_eeprom() */

int
sysd_read_fru_eeprom(fru_eeprom_t *fru_eeprom)
{
    bool            rc;
#if defined(USE_SW_FRU) || defined(PLATFORM_SIMULATION)
    /* Populate stub generic-x86 EEPROM info */
    rc = sysd_cfg_yaml_get_fru_info(fru_eeprom);
    if (0 > rc) {
        VLOG_ERR("Error getting yaml fru info. rc = %d.", rc);
        return -1;
    }
    VLOG_INFO("Retrieved fru info from YAML");
#else
    unsigned char   *buf;
    int             len;
    uint16_t        total_len;
    fru_header_t    header;

    VLOG_INFO("Getting fru info from EEPROM");

    /* Read header info */
    rc = sysd_cfg_yaml_fru_read((unsigned char *) &header, sizeof(header));
    if (!rc) {
        VLOG_ERR("Error reading FRU EEPROM Header");
        log_event("SYS_FRU_EEPROM_HEADER_READ_FAILURE", NULL);
        return -1;
    }

    /* Fail if this OCP FRU version is higher than we support */
    if (header.header_version > SUPPORTED_OCP_FRU_EEPROM_VERSION) {
        VLOG_ERR("Unsupported OCP FRU EEPROM version 0x%x; highest supported is 0x%x",
                 header.header_version, SUPPORTED_OCP_FRU_EEPROM_VERSION);
        return -1;
    }

    total_len = (header.total_length[0] << 8) | (header.total_length[1]);
    VLOG_DBG("total_length is %d", total_len);

    /* Using length from header, read remainder of FRU EEPROM */
    len = total_len + sizeof(fru_header_t) + 1;
    buf = (unsigned char *) calloc(1, len);
    if ((unsigned char *)NULL == buf) {
        VLOG_ERR("Unable to allocate memory for eeprom read");
        return -1;
    }

    rc = sysd_cfg_yaml_fru_read(buf, len);
    if (!rc) {
        VLOG_ERR("Error reading FRU EEPROM");
        return -1;
    }

    /* Populate EEPROM struct */
    rc = sysd_process_eeprom(buf, fru_eeprom, total_len);
    if (!rc) {
        VLOG_ERR("Error processing FRU EEPROM info");
        return -1;
    }
    free(buf);
#endif

    return 0;
} /* sysd_read_fru_eeprom() */
/** @} end of group sysd */
