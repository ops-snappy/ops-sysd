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
 ***************************************************************************/
/*
 * @ingroup ops-sysd
 *
 * @file
 * Source for ops-sysd daemon.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <command-line.h>
#include <dirs.h>
#include <smap.h>
#include <poll-loop.h>
#include <ovsdb-idl.h>
#include <vswitch-idl.h>
#include <openvswitch/vlog.h>
#include <unixctl.h>
#include <daemon.h>
#include <fatal-signal.h>
#include <dynamic-string.h>

#include <ops-utils.h>
#include <config-yaml.h>
#include "sysd_cfg_yaml.h"
#include "sysd.h"
#include "sysd_util.h"
#include "sysd_ovsdb_if.h"

#include "eventlog.h"
#include "diag_dump.h"

VLOG_DEFINE_THIS_MODULE(ops_sysd);

/** @ingroup ops-sysd
 * @{ */

#define BUF_LEN 16000
#define MAX_ERR_STR_LEN 255

/* OVSDB IDL used to obtain configuration. */
struct ovsdb_idl *idl;
uint32_t         idl_seqno = 0;
int              num_subsystems = 0;
sysd_subsystem_t **subsystems = NULL;

char *g_hw_desc_dir = "/";
char *g_hw_desc_link = "/";

daemon_info_t **daemons = NULL;
int num_daemons = 0;
int num_hw_daemons = 0;

/* Structure to store management info read */
mgmt_intf_info_t *mgmt_intf = NULL;

/*
 * Function       : sysd_diag_dump_basic_cb
 * Responsibility : callback handler function for diagnostic dump basic
 *                  it allocates memory as per requirment and populates data.
 *                  INIT_DIAG_DUMP_BASIC will free allocated memory.
 * Parameters     : feature name string, buffer ptr
 * Returns        : void
 */

static void
sysd_diag_dump_basic_cb(const char *feature , char **buf)
{
    if (!buf)
        return;
    *buf =  xcalloc(1, BUF_LEN);
    if (*buf) {
        /* populate basic diagnostic data to buffer  */
        sysd_dump(*buf, BUF_LEN);
        VLOG_DBG("basic diag-dump data populated for feature %s",
                feature);
    } else{
        VLOG_ERR("Memory allocation failed for feature %s, %d bytes",
                feature, BUF_LEN);
    }
}

/* Dumps debug data for entire daemon */
static void
sysd_unixctl_dump(struct unixctl_conn *conn, int argc OVS_UNUSED,
                          const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    char err_str[MAX_ERR_STR_LEN];
    char *buf = xcalloc(1, BUF_LEN);

    /* Dump the daemon info */
    if (buf){
        strcpy(buf, "Support Dump for Platform SYS Daemon (ops-sysd)\n\n");
        sysd_dump(buf, BUF_LEN);
        unixctl_command_reply(conn, buf);
        free(buf);
    } else {
        snprintf(err_str, sizeof(err_str),
                "sysd daemon failed to allocate %d bytes\n", BUF_LEN);
        unixctl_command_reply(conn, err_str);
    }
} /* sysd_unixctl_dump */

static int
sysd_get_subsystem_info(void)
{
    int       i = 0;
    int       rc = 0;

    sysd_subsystem_t    *ptr;

    /* OPS_TODO: Will need to implement mechanism to locate
     *           the hardware description files for this system
     *           and all defined subsystems for this system.
     *           For now, just assume pizza box. */

    num_subsystems = 1;

    subsystems = (sysd_subsystem_t **) calloc(num_subsystems, sizeof(sysd_subsystem_t *));
    if (subsystems == (sysd_subsystem_t **)NULL) {
        VLOG_ERR("Unable to allocate memory for subsystems, terminating");
        return -1;
    }

    for (i = 0; i < num_subsystems; i++) {
        subsystems[i] = (sysd_subsystem_t *) calloc(1, sizeof(sysd_subsystem_t));
        if (subsystems[i] == (sysd_subsystem_t *)NULL) {
            VLOG_ERR("Unable to allocate memory for subsystems, terminating");
            return -1;
        }
    }

    rc = sysd_read_fru_eeprom(&(subsystems[0]->fru_eeprom));
    if (rc) {
        VLOG_ERR("Failed to read FRU data from base system.");
        log_event("SYS_FRU_DATA_READ_FAILURE", NULL);
        return -1;
    }

    /* Store information about BASE subsystem. */
    ptr = subsystems[0];
    strncpy(ptr->name, SYSD_BASE_SUBSYSTEM, MAX_SUBSYSTEM_NAME_LEN);
    ptr->type = SYSD_SUBSYSTEM_TYPE_SYSTEM;

    ptr->num_free_macs = ptr->fru_eeprom.num_macs;
    ptr->nxt_mac_addr = ops_char_array_to_ulong_long(ptr->fru_eeprom.base_mac_address, ETH_ALEN);

    if (ptr->num_free_macs > 0) {
        /* Save first MAC as the mgmt i/f MAC for the system */
        ptr->mgmt_mac_addr = ptr->nxt_mac_addr;
        ptr->num_free_macs--;
        ptr->nxt_mac_addr++;
    }

    if (ptr->num_free_macs > 0) {
        /* Save second MAC as the system MAC */
        ptr->system_mac_addr = ptr->nxt_mac_addr;
        ptr->num_free_macs--;
        ptr->nxt_mac_addr++;
    }

    return 0;

} /* sysd_get_subsystem_info() */

static int
sysd_get_interface_info(void)
{
    int         idx = 0;
    int         intf_count = 0;

    sysd_intf_info_t            **interfaces = NULL;
    sysd_intf_cmn_info_t        *intf_cmn_info = NULL;
    sysd_subsystem_t            *ptr;

    /* Get interface related global info. */
    intf_cmn_info = sysd_cfg_yaml_get_port_subsys_info();
    if (intf_cmn_info == (sysd_intf_cmn_info_t *)NULL) {
        VLOG_ERR("Failed to get interface sub-system info.");
        return -1;
    }

    intf_count = sysd_cfg_yaml_get_port_count();
    if (intf_count <= 0) {
        VLOG_ERR("Unable to get interface count from YAML files.");
        return -1;
    }

    /* Allocate memory for 'intf_count' number of sysd_intf_info_t pointers. */
    interfaces = (sysd_intf_info_t **) calloc(intf_count, sizeof(sysd_intf_info_t *));
    if (interfaces == (sysd_intf_info_t **)NULL) {
        VLOG_ERR("Failed to allocate memory for interface strcture.");
        log_event("SYS_ALLOCATE_MEMORY_FAILURE", EV_KV("value",
            "%s", "interface structure"));
        return -1;
    }

    /* Get info for each interface. */
    for (idx = 0 ; idx < intf_count; idx++) {
        interfaces[idx] = sysd_cfg_yaml_get_port_info(idx);
        if (NULL == interfaces[idx]) {
            VLOG_ERR("Unable to get interface info for interface index %d", idx);
            return -1;
        }
    }

    /* OPS_TODO: Enhance the code to support multiple subsystems. */
    ptr = subsystems[0];
    ptr->intf_count = intf_count;
    ptr->intf_cmn_info = intf_cmn_info;
    ptr->interfaces = interfaces;

    return 0;

} /* sysd_get_interface_info */

static int
sysd_find_hw_desc_files(void)
{
    int rc = 0;

    /* Locate manufacturer/product_name */
    rc = sysd_create_link_to_hwdesc_files();
    if (rc) {
        VLOG_ERR("Unable to determine manufacturer/product_name"
                 "for this platform");
        return -1;
    }
    return (rc);

} /* sysd_find_hw_desc_files() */

void
sysd_ovsdb_conn_init(char *remote)
{
    /* Create connection to database. */
    idl = ovsdb_idl_create(remote, &ovsrec_idl_class, false, true);
    idl_seqno = ovsdb_idl_get_seqno(idl);
    ovsdb_idl_set_lock(idl, "ops_sysd");

    ovsdb_idl_add_table(idl, &ovsrec_table_system);
    ovsdb_idl_add_column(idl, &ovsrec_system_col_subsystems);
    ovsdb_idl_omit_alert(idl, &ovsrec_system_col_subsystems);
    ovsdb_idl_add_column(idl, &ovsrec_system_col_cur_hw);
    ovsdb_idl_omit_alert(idl, &ovsrec_system_col_cur_hw);
    ovsdb_idl_add_column(idl, &ovsrec_system_col_next_hw);
    ovsdb_idl_omit_alert(idl, &ovsrec_system_col_next_hw);
    ovsdb_idl_add_column(idl, &ovsrec_system_col_software_info);
    ovsdb_idl_omit_alert(idl, &ovsrec_system_col_software_info);
    ovsdb_idl_add_column(idl, &ovsrec_system_col_switch_version);
    ovsdb_idl_omit_alert(idl, &ovsrec_system_col_switch_version);

    ovsdb_idl_add_table(idl, &ovsrec_table_subsystem);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_name);
    ovsdb_idl_omit_alert(idl, &ovsrec_subsystem_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_asset_tag_number);
    ovsdb_idl_omit_alert(idl, &ovsrec_subsystem_col_asset_tag_number);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_hw_desc_dir);
    ovsdb_idl_omit_alert(idl, &ovsrec_subsystem_col_hw_desc_dir);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_other_config);
    ovsdb_idl_omit_alert(idl, &ovsrec_subsystem_col_other_config);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_interfaces);
    ovsdb_idl_omit_alert(idl, &ovsrec_subsystem_col_interfaces);

    ovsdb_idl_add_table(idl, &ovsrec_table_interface);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_name);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_hw_intf_info);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_hw_intf_info);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_type);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_type);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_user_config);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_user_config);

    /* Daemon Table */
    ovsdb_idl_add_table(idl, &ovsrec_table_daemon);
    ovsdb_idl_add_column(idl, &ovsrec_daemon_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_daemon_col_cur_hw);
    ovsdb_idl_add_column(idl, &ovsrec_daemon_col_is_hw_handler);
    ovsdb_idl_omit_alert(idl, &ovsrec_daemon_col_is_hw_handler);

    /* Management Interface Column*/
    ovsdb_idl_add_column(idl, &ovsrec_system_col_mgmt_intf);

    /* Package_Info Table */
    ovsdb_idl_add_table(idl, &ovsrec_table_package_info);
    ovsdb_idl_add_column(idl, &ovsrec_package_info_col_name);
    ovsdb_idl_omit_alert(idl, &ovsrec_package_info_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_package_info_col_src_type);
    ovsdb_idl_omit_alert(idl, &ovsrec_package_info_col_src_type);
    ovsdb_idl_add_column(idl, &ovsrec_package_info_col_src_url);
    ovsdb_idl_omit_alert(idl, &ovsrec_package_info_col_src_url);
    ovsdb_idl_add_column(idl, &ovsrec_package_info_col_version);
    ovsdb_idl_omit_alert(idl, &ovsrec_package_info_col_version);

    INIT_DIAG_DUMP_BASIC(sysd_diag_dump_basic_cb);

    return;

} /* sysd_ovsdb_conn_init */

static void
usage(void)
{
    printf("%s: OpenSwitch system daemon\n"
           "usage: %s [OPTIONS] [DATABASE]\n"
           "where DATABASE is a socket on which ovsdb-server is listening\n"
           "      (default: \"unix:%s/db.sock\").\n",
           program_name, program_name, ovs_rundir());
    daemon_usage();
    vlog_usage();
    printf("\nOther options:\n"
           "  --unixctl=SOCKET        override default control socket name\n"
           "  -h, --help              display this help message\n");
    exit(EXIT_SUCCESS);

} /* usage */

static char *
parse_options(int argc, char *argv[], char **unixctl_pathp)
{
    enum {
        OPT_PEER_CA_CERT = UCHAR_MAX + 1,
        OPT_UNIXCTL,
        VLOG_OPTION_ENUMS,
        OPT_BOOTSTRAP_CA_CERT,
        OPT_ENABLE_DUMMY,
        OPT_DISABLE_SYSTEM,
        DAEMON_OPTION_ENUMS,
        OPT_DPDK,
    };
    static const struct option long_options[] = {
        {"help",        no_argument, NULL, 'h'},
        {"unixctl",     required_argument, NULL, OPT_UNIXCTL},
        DAEMON_LONG_OPTIONS,
        VLOG_LONG_OPTIONS,
        {NULL, 0, NULL, 0},
    };
    char *short_options = long_options_to_short_options(long_options);

    for (;;) {
        int c;

        c = getopt_long(argc, argv, short_options, long_options, NULL);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'h':
            usage();

        case OPT_UNIXCTL:
            *unixctl_pathp = optarg;
            break;

        VLOG_OPTION_HANDLERS
        DAEMON_OPTION_HANDLERS

        case '?':
            exit(EXIT_FAILURE);

        default:
            abort();
        }
    }
    free(short_options);

    argc -= optind;
    argv += optind;

    switch (argc) {
    case 0:
        return xasprintf("unix:%s/db.sock", ovs_rundir());

    case 1:
        return xstrdup(argv[0]);

    default:
        VLOG_FATAL("at most one non-option argument accepted; "
                   "use --help for usage");
    }
} /* parse_options */

static void
sysd_exit(struct unixctl_conn *conn, int argc OVS_UNUSED,
                const char *argv[] OVS_UNUSED, void *exiting_)
{
    bool *exiting = exiting_;
    *exiting = true;
    unixctl_command_reply(conn, NULL);

} /* sysd_exit */

int
main(int argc, char *argv[])
{
    char    *appctl_path = NULL;
    char    *ovsdb_sock = NULL;
    int     rc = 0;
    int     exiting = 0;
    int     retval;

    struct unixctl_server   *appctl = NULL;

    set_program_name(argv[0]);
    fatal_ignore_sigpipe();

    /* Parse commandline args and get the name of the OVSDB socket. */
    ovsdb_sock = parse_options(argc, argv, &appctl_path);

    /* Initialize OVSDB metadata. */
    ovsrec_init();

    /* Fork and return in child process; but don't notify parent of
     * startup completion yet. */
    daemonize_start();

    retval = event_log_init("SYS");
    if(retval < 0) {
        VLOG_ERR("Event log initialization failed for SYS");
    }

    /* Create UDS connection for ovs-appctl. */
    rc = unixctl_server_create(appctl_path, &appctl);
    if (rc) {
        exit(EXIT_FAILURE);
    }

    /* Register ovs-appctl commands for this daemon. */
    unixctl_command_register("ops-sysd/dump", "", 0, 0, sysd_unixctl_dump, NULL);

    /* Register the ovs-appctl "exit" command for this daemon. */
    unixctl_command_register("exit", "", 0, 0, sysd_exit, &exiting);

    sysd_ovsdb_conn_init(ovsdb_sock);
    free(ovsdb_sock);

    /* Process the manifest file */
    rc = sysd_read_manifest_file();
    if (rc) {
        VLOG_ERR("Unable to process image.manifest file.");
        exit(-1);
    }

    /* Determine the platform we are on and
     * locate H/W desc files. */
    rc = sysd_find_hw_desc_files();
    if (rc) {
        VLOG_ERR("Unable to find HW descriptor files.");
        exit(-1);
    }

    /* OPS_TODO: Need to refactor to not die if h/w desc info
     * is not available. Can do this when adding subsystem support. */

    /* Initialize and parse needed yaml files. */
    rc = sysd_cfg_yaml_init(g_hw_desc_dir);
    if (!rc) {
        VLOG_ERR("Unable to initialize YAML config files.");
        exit(-1);
    }

    rc = sysd_get_subsystem_info();
    if (rc) {
        VLOG_ERR("Unable to enumerate subsystems in the system.");
        exit(-1);
    }

    rc = sysd_get_interface_info();
    if (rc) {
        VLOG_ERR("Unable to enumerate interfaces in the system.");
        exit(-1);
    }

    while (!exiting) {
        sysd_run();
        unixctl_server_run(appctl);

        sysd_wait();
        unixctl_server_wait(appctl);
        if (exiting) {
            poll_immediate_wake();
        } else {
            poll_block();
        }
    }

    return 0;

} /* main */
/** @} end of group ops-sysd */
