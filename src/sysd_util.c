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
 * Source for sysd utility functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <zlib.h>

#include "util.h"
#include "openvswitch/vlog.h"
#include "json.h"
#include "sysd_util.h"

#include <ops-utils.h>
#include <config-yaml.h>
#include "sysd_cfg_yaml.h"
#include "sysd.h"

/***********************************************************/

#define DMIDECODE_NAME "dmidecode"

VLOG_DEFINE_THIS_MODULE(sysd_util);

/** @ingroup sysd
 * @{ */

struct json *manifest_info = NULL;

#ifndef PLATFORM_SIMULATION
static int
dmidecode_exists(char *cmd_path)
{
    char          *paths[] = {"/usr/sbin", "/sbin",
                              "/bin", "/usr/bin"};
    static char   buf[50];
    unsigned int  i = 0;
    struct stat   sbuf;

    /* Look for "dmidecode" command */
    for (i = 0; i < sizeof(paths); i++) {
        snprintf(buf, sizeof(buf), "%s/%s",
                 paths[i], DMIDECODE_NAME);

        if ((stat(buf, &sbuf) == 0) &&
            (sbuf.st_mode & S_IXUSR)) {
           strncpy(cmd_path, buf, sizeof(buf));
           return 0;
        }
    }
    return -1;
} /* dmidecode_exists() */
#endif /* PLATFORM_SIMULATION */

void
get_sys_cmd_out(char *cmd, char **output)
{
    FILE        *fd = NULL;
    char        *buf = NULL;
    size_t      nbytes = 0, size = 0;

    *output = NULL;

    fd = popen(cmd, "r");
    if (fd == (FILE *) NULL) {
        VLOG_ERR("popen failed for %s: Error: %s", cmd, ovs_strerror(errno));
        return;
    }

    while (1) {

        buf = NULL;
        size = 0;
        nbytes = getline(&buf, &size, fd);
        if (nbytes <= 0) {
            VLOG_ERR("Failed to parse output. rc=%s", ovs_strerror(errno));
            return;
        }

        /* Ignore the comments that starts with '#'. */
        if (buf[0] == '#') {
            continue;
        } else if (buf[0] != '\0') {
            /* Return the buffer, caller will free the buffer. */
            buf[nbytes-1] = '\0';
            *output = buf;
            break;
        }
        /* getline allocates buffer, it should be freed. */
        free(buf);
    }
    pclose(fd);

} /* get_sys_cmd_out */

#ifndef PLATFORM_SIMULATION
static void
get_manuf_and_prodname(char *cmd_path, char **manufacturer, char **product_name)
{
    char    dmid_cmd[256];

    snprintf(dmid_cmd, sizeof(dmid_cmd), "%s -s %s",
             cmd_path, "system-manufacturer");

    get_sys_cmd_out(dmid_cmd, manufacturer);
    if (*manufacturer == NULL) {
        VLOG_ERR("Unable to get system manufacturer.");
        return;
    }

    snprintf(dmid_cmd, sizeof(dmid_cmd), "%s -s %s",
             cmd_path, "system-product-name");

    get_sys_cmd_out(dmid_cmd, product_name);
    if (*product_name == NULL) {
        VLOG_ERR("Unable to get system product name.");
        return;
    }
    return;

} /* get_manuf_and_prodname() */
#endif	/* PLATFORM_SIMULATION */

static int
create_link_to_desc_files(char *manufacturer, char *product_name)
{
    char        path[1024];
    int         rc = 0;
    struct stat sbuf;
    extern char *g_hw_desc_dir;
    extern char *g_hw_desc_link;

    snprintf(path, sizeof(path), "%s%s/%s/%s",
             g_install_rootdir, HWDESC_FILES_PATH, manufacturer, product_name);

    VLOG_INFO("Location to HW descrptor files: %s", path);

    g_hw_desc_dir = strdup(path);

    if (stat(g_hw_desc_dir, &sbuf) != 0) {
        VLOG_ERR("Unable to find hardware description files at %s", g_hw_desc_dir);
        return -1;
    }

    /* Remove old link if it exists */
    snprintf(path, sizeof(path), "%s%s", g_data_rootdir, HWDESC_FILE_LINK);
    g_hw_desc_link = strdup(path);
    remove(g_hw_desc_link);

    /* mkdir for the new link */
    snprintf(path, sizeof(path), "%s%s", g_data_rootdir, HWDESC_FILE_LINK_PATH);
    rc = mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (rc == -1 && errno != EEXIST) {
        VLOG_ERR("Failed to create %s, Error %s", path, ovs_strerror(errno));
        return -1;
    }

    /* Create link to these files */
    if (-1 == symlink(g_hw_desc_dir, g_hw_desc_link)) {
        VLOG_ERR("Unable to create  soft link to %s -> %s. Error %s",
                 g_hw_desc_link, g_hw_desc_dir, ovs_strerror(errno));
        return -1;
    }

    return 0;

} /* create_link_to_desc_files */

int
sysd_create_link_to_hwdesc_files(void)
{
    char    *manufacturer = NULL;
    char    *product_name = NULL;
    char    cmd_path[50];
    int     rc = 0;

    memset(cmd_path, 0, sizeof(cmd_path));

#ifdef PLATFORM_SIMULATION
    /* For x86/simulation assign the manufacturer and product name */
    manufacturer = strdup(GENERIC_X86_MANUFACTURER);
    product_name = strdup(GENERIC_X86_PRODUCT_NAME);
#else
    /* OPS_TODO: Add other methods to find manuf/prodname.
     * Run dmidecode command (if it exists) to get system info. */
    rc = dmidecode_exists(cmd_path);
    if (rc) {
        VLOG_ERR("Unable to locate \"dmidecode\" command");
        return -1;
    }

    get_manuf_and_prodname(cmd_path, &manufacturer, &product_name);
    if ((manufacturer == NULL) || (product_name == NULL)) {
        return -1;
    }
#endif

    VLOG_DBG("manufacturer=%s product_name=%s", manufacturer, product_name);

    rc = create_link_to_desc_files(manufacturer, product_name);
    if (rc) {
        VLOG_ERR("Failed to create link to HW descriptor files");
        return -1;
    }

    free(manufacturer);
    free(product_name);

    return 0;

} /* sysd_create_link_to_hwdesc_files */

unsigned int
calc_crc(unsigned char *buf, int len)
{
    unsigned int crc;

    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, buf, len);

    return (crc);

} /* calc_crc() */

static void
_sysd_get_hw_handler(struct shash *object) {
    const struct shash_node *dnode;
    struct json *jp;

    SHASH_FOR_EACH (dnode, object) {
        if (strncmp(dnode->name, HW_HANDLER_TAG, strlen(HW_HANDLER_TAG))
                                                        == 0) {
            jp = dnode->data;
            switch (jp->type) {
                case JSON_TRUE:
                    daemons[num_daemons]->is_hw_handler = true;
                    break;
                case JSON_FALSE:
                    /* Fall through */
                default:
                    daemons[num_daemons]->is_hw_handler = false;
                    break;
            }
            return;
        }
    }
    return;
} /*_sysd_get_hw_handler() */

static int
_sysd_process_daemons(struct shash *object) {
    const struct shash_node *dnode;

    SHASH_FOR_EACH (dnode, object) {
        daemons = realloc(daemons, sizeof(daemon_info_t*)*(num_daemons+1));
        daemons[num_daemons] = calloc(sizeof(daemon_info_t), 1);
        if (daemons[num_daemons] == (daemon_info_t *) NULL) {
            VLOG_ERR("Error processing daemon information");
            return (-1);
        }

        strncpy(daemons[num_daemons]->name, dnode->name, strlen(dnode->name));

        /* If this row is sysd, then go ahead and set cur_hw = 1 since
           ...everything is being done in one transaction. */

        if (strncmp(daemons[num_daemons]->name, NAME_IN_DAEMON_TABLE,
                    strlen(NAME_IN_DAEMON_TABLE)) == 0 ) {
            daemons[num_daemons]->cur_hw = 1;
        }
        else
        {
            daemons[num_daemons]->cur_hw = 0;
        }

        _sysd_get_hw_handler(json_object(dnode->data));

        num_daemons++;
    }

    return(0);
} /*_sysd_process_daemons() */


static int
_sysd_process_mgmt_intf(struct shash *object)
{
    const struct shash_node *dnode;
    struct json *jp;
    static bool is_mgmt_intf_present = false;

    SHASH_FOR_EACH (dnode, object) {
        if (strncmp(dnode->name, MGMT_INTF_NAME_TAG, strlen(MGMT_INTF_NAME_TAG)) == 0) {
            jp = dnode->data;
            switch (jp->type) {
                case JSON_STRING:
                    mgmt_intf = xcalloc(sizeof(mgmt_intf_info_t), 0);

                    strncpy(mgmt_intf->name,jp->u.string,strlen(jp->u.string));
                    is_mgmt_intf_present = true;
                    VLOG_DBG("Management Interface read successfully: %s",mgmt_intf->name);
                    break;
                default:
                    VLOG_ERR("Error retreiving management interface name");
                    break;
            }

            if (is_mgmt_intf_present == false)
            {
                VLOG_ERR("Management interface not present in image.manifest file");
                return -1;
            }

            return 0;
        }
    }

    if (is_mgmt_intf_present == false)
    {
        VLOG_ERR("Management interface not present in image.manifest file");
        return -1;
    }

    return(0);
} /*_sysd_process_mgmt_intf() */


int
sysd_process_json(struct json *json)
{
    struct shash *object;
    struct json_array *array;
    const struct shash_node *node;
    struct json *jp;
    int i;

    /* The approach is to recursively look for type==OBJECT and then
       ...see if it is what we care about (for now, only "dameons").
       ...When we find something we care about, call a specific function
       ...to process it. */

    switch (json->type) {
        case JSON_OBJECT:
            object = json_object(json);

            SHASH_FOR_EACH (node, object) {
                jp = node->data;
                if (jp->type == JSON_OBJECT) {
                    if (strncmp(node->name, DAEMONS_TAG, strlen(DAEMONS_TAG))
                                                        == 0) {
                        if (_sysd_process_daemons(json_object(node->data))) {
                            return (-1);
                        }
                    }
                    else if (strncmp(node->name, MGMT_INTF_TAG, strlen(MGMT_INTF_TAG)) == 0)
                    {
                        if (_sysd_process_mgmt_intf(json_object(node->data)))
                        {
                            return (-1);
                        }
                    }
                    else
                    {
                        if (sysd_process_json(jp)) {
                            return (-1);
                        }
                    }
                }
                else
                {
                    if (jp->type == JSON_ARRAY) {
                        /* Don't yet care about anything that is an array
                           ...so just keep looking in case what we do care
                           ...about is nested inside an array. */
                        if (sysd_process_json(jp)) {
                            return (-1);
                        }
                    }
                }
                /* Ignore anything that isn't an object or array. */
            }
            break;
        case JSON_ARRAY:
            /* Don't yet care about anything that is an array
               ...so just keep looking in case what we do care
               ...about is nested inside an array. */

            array = json_array(json);

            for (i = 0; i < (int) array->n; i++) {
                jp = array->elems[i];
                if (sysd_process_json(jp)) {
                    return (-1);
                }
            }
            break;
        default:
            /* Ignore anything that isn't an object or array. */
            break;
    }

    return(0);
} /* sysd_process_json() */

static void
sysd_set_num_hw_daemons()
{
    int i;

    num_hw_daemons = 0;
    for (i = 0; i < num_daemons; i++) {
        if (daemons[i]->is_hw_handler) {
            num_hw_daemons++;
        }
    }
    return;
} /* sysd_set_num_hw_daemons() */

int
sysd_read_manifest_file(void)
{
    char image_manifest_path[1024];
    snprintf(image_manifest_path, sizeof(image_manifest_path), "%s%s",
             g_install_rootdir, IMAGE_MANIFEST_FILE_PATH);
    manifest_info = json_from_file(image_manifest_path);

    if (manifest_info == (struct json *) NULL) {
        return -1;
    }

    /* The top level JSON blob must be either OBJECT or ARRAY. */
    if ((manifest_info->type != JSON_OBJECT) &&
                            (manifest_info->type != JSON_ARRAY)) {
        VLOG_ERR("invalid JSON type of %d", (int)manifest_info->type);
        return (-1);
    }

    if (sysd_process_json(manifest_info)) {
        VLOG_ERR("Error processing %s", IMAGE_MANIFEST_FILE_PATH);
        return(-1);
    }

    json_destroy(manifest_info);

    sysd_set_num_hw_daemons();

    return(0);
} /* sysd_read_manifest_file() */
/** @} end of group sysd */
