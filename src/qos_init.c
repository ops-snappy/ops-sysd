/****************************************************************************
 * (c) Copyright 2015-2016 Hewlett Packard Enterprise Development LP
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

#include <config-yaml.h>
#include <ops-utils.h>
#include <stdlib.h>
#include <sys/types.h>

#include "openvswitch/vlog.h"
#include "qos_init.h"
#include "sysd_qos_utils.h"
#include "smap.h"
#include "util.h"
#include "vswitch-idl.h"

VLOG_DEFINE_THIS_MODULE(qos_init);

struct ovsdb_idl *idl;

/**************************************************************/

static bool has_local_priority(
        struct ovsrec_q_profile_entry *queue_row,
        int64_t local_priority) {
    int i;
    for (i = 0; i < queue_row->n_local_priorities; i++) {
        if (queue_row->local_priorities[i] == local_priority) {
            return true;
        }
    }

    return false;
}

static struct ovsrec_q_profile *qos_get_queue_profile_row(
        const char *profile_name) {
    const struct ovsrec_q_profile *profile_row;
    OVSREC_Q_PROFILE_FOR_EACH(profile_row, idl) {
        if (strcmp(profile_row->name, profile_name) == 0) {
            return (struct ovsrec_q_profile *) profile_row;
        }
    }

    return NULL;
}

static struct ovsrec_q_profile_entry *qos_get_queue_profile_entry_row(
        struct ovsrec_q_profile *profile_row, int64_t queue_num) {
    int i;
    for (i = 0; i < profile_row->n_q_profile_entries; i++) {
        if (profile_row->key_q_profile_entries[i] == queue_num) {
            return profile_row->value_q_profile_entries[i];
        }
    }

    return NULL;
}

static struct ovsrec_q_profile_entry *insert_queue_profile_queue_row(
        struct ovsrec_q_profile *profile_row, int64_t queue_num,
        struct ovsdb_idl_txn *txn) {
    /* Create the queue row. */
    struct ovsrec_q_profile_entry *queue_row =
            ovsrec_q_profile_entry_insert(txn);

    /* Update the profile row. */
    int64_t *key_list =
            xmalloc(sizeof(int64_t) *
                    (profile_row->n_q_profile_entries + 1));
    struct ovsrec_q_profile_entry **value_list =
            xmalloc(sizeof *profile_row->value_q_profile_entries *
                    (profile_row->n_q_profile_entries + 1));

    int i;
    for (i = 0; i < profile_row->n_q_profile_entries; i++) {
        key_list[i] = profile_row->key_q_profile_entries[i];
        value_list[i] = profile_row->value_q_profile_entries[i];
    }
    key_list[profile_row->n_q_profile_entries] = queue_num;
    value_list[profile_row->n_q_profile_entries] = queue_row;
    ovsrec_q_profile_set_q_profile_entries(profile_row, key_list,
            value_list, profile_row->n_q_profile_entries + 1);
    free(key_list);
    free(value_list);

    return queue_row;
}

static bool qos_queue_profile_command(
        struct ovsdb_idl_txn *txn,
        const char *profile_name) {
    /* Retrieve the row. */
    struct ovsrec_q_profile *profile_row =
            qos_get_queue_profile_row(profile_name);
    if (profile_row == NULL) {
        /* Create a new row. */
        profile_row = ovsrec_q_profile_insert(txn);
        ovsrec_q_profile_set_name(profile_row, profile_name);
    }

    return false;
}

static void add_local_priority(struct ovsrec_q_profile_entry *queue_row,
        int64_t local_priority) {
    if (has_local_priority(queue_row, local_priority)) {
        return;
    }

    /* local_priority was not found, so add it. */
    int64_t *value_list =
            xmalloc(sizeof(int64_t) *
                    (queue_row->n_local_priorities + 1));
    int i;
    for (i = 0; i < queue_row->n_local_priorities; i++) {
        value_list[i] = queue_row->local_priorities[i];
    }
    value_list[queue_row->n_local_priorities] = local_priority;
    ovsrec_q_profile_entry_set_local_priorities(
            queue_row, value_list, queue_row->n_local_priorities + 1);
    free(value_list);
}

static bool qos_queue_profile_map_command(
        struct ovsdb_idl_txn *txn, const char *profile_name,
        int64_t queue_num, int64_t local_priority) {
    /* Retrieve the profile row. */
    struct ovsrec_q_profile *profile_row =
            qos_get_queue_profile_row(profile_name);
    if (profile_row == NULL) {
        VLOG_ERR("Profile %s does not exist.",
                profile_name);
        return true;
    }

    /* Retrieve the existing queue row. */
    struct ovsrec_q_profile_entry *queue_row =
            qos_get_queue_profile_entry_row(profile_row, queue_num);

    /* If no existing row, then insert a new queue row. */
    if (queue_row == NULL) {
        queue_row = insert_queue_profile_queue_row(profile_row, queue_num, txn);
    }

    /* Update the queue row. */
    add_local_priority(queue_row, local_priority);

    return false;
}

static void qos_queue_profile_create_factory_default(
        struct ovsdb_idl_txn *txn,
        const char *default_name) {
    qos_queue_profile_command(txn, default_name);

    qos_queue_profile_map_command(txn, default_name, 7, 7);
    qos_queue_profile_map_command(txn, default_name, 6, 6);
    qos_queue_profile_map_command(txn, default_name, 5, 5);
    qos_queue_profile_map_command(txn, default_name, 4, 4);
    qos_queue_profile_map_command(txn, default_name, 3, 3);
    qos_queue_profile_map_command(txn, default_name, 2, 2);
    qos_queue_profile_map_command(txn, default_name, 1, 1);
    qos_queue_profile_map_command(txn, default_name, 0, 0);
}

/**************************************************************/

static struct ovsrec_qos *qos_get_schedule_profile_row(
        const char *profile_name) {
    const struct ovsrec_qos *profile_row;
    OVSREC_QOS_FOR_EACH(profile_row, idl) {
        if (strcmp(profile_row->name, profile_name) == 0) {
            return (struct ovsrec_qos *) profile_row;
        }
    }

    return NULL;
}

static struct ovsrec_queue *qos_get_schedule_profile_entry_row(
        struct ovsrec_qos *profile_row, int64_t queue_num) {
    int i;
    for (i = 0; i < profile_row->n_queues; i++) {
        if (profile_row->key_queues[i] == queue_num) {
            return profile_row->value_queues[i];
        }
    }

    return NULL;
}

static struct ovsrec_queue *insert_schedule_profile_queue_row(
        struct ovsrec_qos *profile_row, int64_t queue_num,
        struct ovsdb_idl_txn *txn) {
    /* Create the queue row. */
    struct ovsrec_queue *queue_row =
            ovsrec_queue_insert(txn);

    /* Update the profile row. */
    int64_t *key_list =
            xmalloc(sizeof(int64_t) *
                    (profile_row->n_queues + 1));
    struct ovsrec_queue **value_list =
            xmalloc(sizeof *profile_row->value_queues *
                    (profile_row->n_queues + 1));

    int i;
    for (i = 0; i < profile_row->n_queues; i++) {
        key_list[i] = profile_row->key_queues[i];
        value_list[i] = profile_row->value_queues[i];
    }
    key_list[profile_row->n_queues] = queue_num;
    value_list[profile_row->n_queues] = queue_row;
    ovsrec_qos_set_queues(profile_row, key_list,
            value_list, profile_row->n_queues + 1);
    free(key_list);
    free(value_list);

    return queue_row;
}

static bool qos_schedule_profile_command(
        struct ovsdb_idl_txn *txn,
        const char *profile_name) {
    /* Retrieve the row. */
    struct ovsrec_qos *profile_row =
            qos_get_schedule_profile_row(profile_name);
    if (profile_row == NULL) {
        /* Create a new row. */
        profile_row = ovsrec_qos_insert(txn);
        ovsrec_qos_set_name(profile_row, profile_name);
    }

    return false;
}

static bool qos_schedule_profile_strict_command(
        struct ovsdb_idl_txn *txn,
        const char *profile_name, int64_t queue_num) {
    /* Retrieve the profile row. */
    struct ovsrec_qos *profile_row =
            qos_get_schedule_profile_row(profile_name);
    if (profile_row == NULL) {
        VLOG_ERR("Profile %s does not exist.", profile_name);
        return true;
    }

    /* Retrieve the existing queue row. */
    struct ovsrec_queue *queue_row =
            qos_get_schedule_profile_entry_row(profile_row, queue_num);

    /* If no existing row, then insert a new queue row. */
    if (queue_row == NULL) {
        queue_row = insert_schedule_profile_queue_row(profile_row, queue_num, txn);
    }

    /* Update the queue row. */
    ovsrec_queue_set_algorithm(queue_row, OVSREC_QUEUE_ALGORITHM_STRICT);
    ovsrec_queue_set_weight(queue_row, NULL, 0);

    return false;
}

static bool qos_schedule_profile_wrr_command(
        struct ovsdb_idl_txn *txn, const char *profile_name,
        int64_t queue_num, int64_t weight) {
    /* Retrieve the profile row. */
    struct ovsrec_qos *profile_row =
            qos_get_schedule_profile_row(profile_name);
    if (profile_row == NULL) {
        VLOG_ERR("Profile %s does not exist.",
                profile_name);
        return true;
    }

    /* Retrieve the existing queue row. */
    struct ovsrec_queue *queue_row =
            qos_get_schedule_profile_entry_row(profile_row, queue_num);

    /* If no existing row, then insert a new queue row. */
    if (queue_row == NULL) {
        queue_row = insert_schedule_profile_queue_row(profile_row, queue_num, txn);
    }

    /* Update the queue row. */
    ovsrec_queue_set_algorithm(queue_row, OVSREC_QUEUE_ALGORITHM_WRR);
    ovsrec_queue_set_weight(queue_row, &weight, 1);

    return false;
}

static void qos_schedule_profile_create_factory_default(
        struct ovsdb_idl_txn *txn,
        const char *default_name) {
    qos_schedule_profile_command(txn, default_name);

    /* Delete all queue rows. */
    struct ovsrec_qos *profile_row =
            qos_get_schedule_profile_row(default_name);
    ovsrec_qos_set_queues(profile_row, NULL,
            NULL, 0);

    /* Create all queue rows. */
    qos_schedule_profile_strict_command(txn, default_name, 7);
    qos_schedule_profile_wrr_command(txn, default_name, 6, 7);
    qos_schedule_profile_wrr_command(txn, default_name, 5, 6);
    qos_schedule_profile_wrr_command(txn, default_name, 4, 5);
    qos_schedule_profile_wrr_command(txn, default_name, 3, 4);
    qos_schedule_profile_wrr_command(txn, default_name, 2, 3);
    qos_schedule_profile_wrr_command(txn, default_name, 1, 2);
    qos_schedule_profile_wrr_command(txn, default_name, 0, 1);
}

/**************************************************************/

void qos_init_trust(struct ovsdb_idl_txn *txn,
                           struct ovsrec_system *system_row) {
    struct smap smap;
    smap_clone(&smap, &system_row->qos_config);
    smap_replace(&smap, QOS_TRUST_KEY, QOS_TRUST_DEFAULT);
    ovsrec_system_set_qos_config(system_row, &smap);
    smap_destroy(&smap);
}

static void set_cos_map_entry(struct ovsrec_qos_cos_map_entry *cos_map_entry,
        int64_t code_point, int64_t local_priority,
        char *color, char *description) {
    /* Initialize the actual config. */
    ovsrec_qos_cos_map_entry_set_code_point(cos_map_entry, code_point);
    ovsrec_qos_cos_map_entry_set_local_priority(cos_map_entry, local_priority);
    ovsrec_qos_cos_map_entry_set_color(cos_map_entry, color);
    ovsrec_qos_cos_map_entry_set_description(cos_map_entry, description);

    char code_point_buffer[QOS_CLI_STRING_BUFFER_SIZE];
    sprintf(code_point_buffer, "%d", (int) code_point);
    char local_priority_buffer[QOS_CLI_STRING_BUFFER_SIZE];
    sprintf(local_priority_buffer, "%d", (int) local_priority);

    /* Save the factory defaults so they can be restored later. */
    struct smap smap;
    smap_clone(&smap, &cos_map_entry->hw_defaults);
    smap_replace(&smap, QOS_DEFAULT_CODE_POINT_KEY, code_point_buffer);
    smap_replace(&smap, QOS_DEFAULT_LOCAL_PRIORITY_KEY, local_priority_buffer);
    smap_replace(&smap, QOS_DEFAULT_COLOR_KEY, color);
    smap_replace(&smap, QOS_DEFAULT_DESCRIPTION_KEY, description);
    ovsrec_qos_cos_map_entry_set_hw_defaults(cos_map_entry, &smap);
    smap_destroy(&smap);
}

static void qos_init_default_cos_map(
        struct ovsrec_qos_cos_map_entry **cos_map) {
    set_cos_map_entry(cos_map[0], 0, 1, "green", "Best_Effort");
    set_cos_map_entry(cos_map[1], 1, 0, "green", "Background");
    set_cos_map_entry(cos_map[2], 2, 2, "green", "Excellent_Effort");
    set_cos_map_entry(cos_map[3], 3, 3, "green", "Critical_Applications");
    set_cos_map_entry(cos_map[4], 4, 4, "green", "Video");
    set_cos_map_entry(cos_map[5], 5, 5, "green", "Voice");
    set_cos_map_entry(cos_map[6], 6, 6, "green", "Internetwork_Control");
    set_cos_map_entry(cos_map[7], 7, 7, "green", "Network_Control");
}

void qos_init_cos_map(struct ovsdb_idl_txn *txn,
                           struct ovsrec_system *system_row) {
    /* Create the cos-map rows. */
    struct ovsrec_qos_cos_map_entry *cos_map_rows[QOS_COS_MAP_ENTRY_COUNT];
    int i;
    for (i = 0; i < QOS_COS_MAP_ENTRY_COUNT; i++) {
        struct ovsrec_qos_cos_map_entry *cos_map_row =
                ovsrec_qos_cos_map_entry_insert(txn);
        cos_map_rows[i] = cos_map_row;
    }

    /* Update the cos-map rows. */
    qos_init_default_cos_map(cos_map_rows);

    /* Update the system row. */
    struct ovsrec_qos_cos_map_entry **value_list = xmalloc(
            sizeof *system_row->qos_cos_map_entries *
            QOS_COS_MAP_ENTRY_COUNT);
    for (i = 0; i < QOS_COS_MAP_ENTRY_COUNT; i++) {
        value_list[i] = cos_map_rows[i];
    }
    ovsrec_system_set_qos_cos_map_entries(system_row, value_list,
            QOS_COS_MAP_ENTRY_COUNT);
    free(value_list);
}

static void set_dscp_map_entry(struct ovsrec_qos_dscp_map_entry *dscp_map_entry,
        int64_t code_point, int64_t local_priority,
        int64_t priority_code_point, char *color, char *description) {
    /* Initialize the actual config. */
    ovsrec_qos_dscp_map_entry_set_code_point(dscp_map_entry, code_point);
    ovsrec_qos_dscp_map_entry_set_local_priority(dscp_map_entry, local_priority);
    ovsrec_qos_dscp_map_entry_set_priority_code_point(dscp_map_entry, &priority_code_point, 1);
    ovsrec_qos_dscp_map_entry_set_color(dscp_map_entry, color);
    ovsrec_qos_dscp_map_entry_set_description(dscp_map_entry, description);

    char code_point_buffer[QOS_CLI_STRING_BUFFER_SIZE];
    sprintf(code_point_buffer, "%d", (int) code_point);
    char local_priority_buffer[QOS_CLI_STRING_BUFFER_SIZE];
    sprintf(local_priority_buffer, "%d", (int) local_priority);
    char priority_code_point_buffer[QOS_CLI_STRING_BUFFER_SIZE];
    sprintf(priority_code_point_buffer, "%d", (int) priority_code_point);

    /* Save the factory defaults so they can be restored later. */
    struct smap smap;
    smap_clone(&smap, &dscp_map_entry->hw_defaults);
    smap_replace(&smap, QOS_DEFAULT_CODE_POINT_KEY, code_point_buffer);
    smap_replace(&smap, QOS_DEFAULT_LOCAL_PRIORITY_KEY, local_priority_buffer);
    smap_replace(&smap, QOS_DEFAULT_PRIORITY_CODE_POINT_KEY, priority_code_point_buffer);
    smap_replace(&smap, QOS_DEFAULT_COLOR_KEY, color);
    smap_replace(&smap, QOS_DEFAULT_DESCRIPTION_KEY, description);
    ovsrec_qos_dscp_map_entry_set_hw_defaults(dscp_map_entry, &smap);
    smap_destroy(&smap);
}

static void qos_init_default_dscp_map(
        struct ovsrec_qos_dscp_map_entry **dscp_map) {
    set_dscp_map_entry(dscp_map[0], 0, 0, 1, "green", "CS0");
    set_dscp_map_entry(dscp_map[1], 1, 0, 1, "green", "");
    set_dscp_map_entry(dscp_map[2], 2, 0, 1, "green", "");
    set_dscp_map_entry(dscp_map[3], 3, 0, 1, "green", "");
    set_dscp_map_entry(dscp_map[4], 4, 0, 1, "green", "");
    set_dscp_map_entry(dscp_map[5], 5, 0, 1, "green", "");
    set_dscp_map_entry(dscp_map[6], 6, 0, 1, "green", "");
    set_dscp_map_entry(dscp_map[7], 7, 0, 1, "green", "");
    set_dscp_map_entry(dscp_map[8], 8, 1, 0, "green", "CS1");
    set_dscp_map_entry(dscp_map[9], 9, 1, 0, "green", "");

    set_dscp_map_entry(dscp_map[10], 10, 1, 0, "green", "AF11");
    set_dscp_map_entry(dscp_map[11], 11, 1, 0, "green", "");
    set_dscp_map_entry(dscp_map[12], 12, 1, 0, "yellow", "AF12");
    set_dscp_map_entry(dscp_map[13], 13, 1, 0, "green", "");
    set_dscp_map_entry(dscp_map[14], 14, 1, 0, "red", "AF13");
    set_dscp_map_entry(dscp_map[15], 15, 1, 0, "green", "");
    set_dscp_map_entry(dscp_map[16], 16, 2, 2, "green", "CS2");
    set_dscp_map_entry(dscp_map[17], 17, 2, 2, "green", "");
    set_dscp_map_entry(dscp_map[18], 18, 2, 2, "green", "AF21");
    set_dscp_map_entry(dscp_map[19], 19, 2, 2, "green", "");

    set_dscp_map_entry(dscp_map[20], 20, 2, 2, "yellow", "AF22");
    set_dscp_map_entry(dscp_map[21], 21, 2, 2, "green", "");
    set_dscp_map_entry(dscp_map[22], 22, 2, 2, "red", "AF23");
    set_dscp_map_entry(dscp_map[23], 23, 2, 2, "green", "");
    set_dscp_map_entry(dscp_map[24], 24, 3, 3, "green", "CS3");
    set_dscp_map_entry(dscp_map[25], 25, 3, 3, "green", "");
    set_dscp_map_entry(dscp_map[26], 26, 3, 3, "green", "AF31");
    set_dscp_map_entry(dscp_map[27], 27, 3, 3, "green", "");
    set_dscp_map_entry(dscp_map[28], 28, 3, 3, "yellow", "AF32");
    set_dscp_map_entry(dscp_map[29], 29, 3, 3, "green", "");

    set_dscp_map_entry(dscp_map[30], 30, 3, 3, "red", "AF33");
    set_dscp_map_entry(dscp_map[31], 31, 3, 3, "green", "");
    set_dscp_map_entry(dscp_map[32], 32, 4, 4, "green", "CS4");
    set_dscp_map_entry(dscp_map[33], 33, 4, 4, "green", "");
    set_dscp_map_entry(dscp_map[34], 34, 4, 4, "green", "AF41");
    set_dscp_map_entry(dscp_map[35], 35, 4, 4, "green", "");
    set_dscp_map_entry(dscp_map[36], 36, 4, 4, "yellow", "AF42");
    set_dscp_map_entry(dscp_map[37], 37, 4, 4, "green", "");
    set_dscp_map_entry(dscp_map[38], 38, 4, 4, "red", "AF43");
    set_dscp_map_entry(dscp_map[39], 39, 4, 4, "green", "");

    set_dscp_map_entry(dscp_map[40], 40, 5, 5, "green", "CS5");
    set_dscp_map_entry(dscp_map[41], 41, 5, 5, "green", "");
    set_dscp_map_entry(dscp_map[42], 42, 5, 5, "green", "");
    set_dscp_map_entry(dscp_map[43], 43, 5, 5, "green", "");
    set_dscp_map_entry(dscp_map[44], 44, 5, 5, "green", "");
    set_dscp_map_entry(dscp_map[45], 45, 5, 5, "green", "");
    set_dscp_map_entry(dscp_map[46], 46, 5, 5, "green", "EF");
    set_dscp_map_entry(dscp_map[47], 47, 5, 5, "green", "");
    set_dscp_map_entry(dscp_map[48], 48, 6, 6, "green", "CS6");
    set_dscp_map_entry(dscp_map[49], 49, 6, 6, "green", "");

    set_dscp_map_entry(dscp_map[50], 50, 6, 6, "green", "");
    set_dscp_map_entry(dscp_map[51], 51, 6, 6, "green", "");
    set_dscp_map_entry(dscp_map[52], 52, 6, 6, "green", "");
    set_dscp_map_entry(dscp_map[53], 53, 6, 6, "green", "");
    set_dscp_map_entry(dscp_map[54], 54, 6, 6, "green", "");
    set_dscp_map_entry(dscp_map[55], 55, 6, 6, "green", "");
    set_dscp_map_entry(dscp_map[56], 56, 7, 7, "green", "CS7");
    set_dscp_map_entry(dscp_map[57], 57, 7, 7, "green", "");
    set_dscp_map_entry(dscp_map[58], 58, 7, 7, "green", "");
    set_dscp_map_entry(dscp_map[59], 59, 7, 7, "green", "");

    set_dscp_map_entry(dscp_map[60], 60, 7, 7, "green", "");
    set_dscp_map_entry(dscp_map[61], 61, 7, 7, "green", "");
    set_dscp_map_entry(dscp_map[62], 62, 7, 7, "green", "");
    set_dscp_map_entry(dscp_map[63], 63, 7, 7, "green", "");
}

void qos_init_dscp_map(struct ovsdb_idl_txn *txn,
                           struct ovsrec_system *system_row) {
    /* Create the dscp-map rows. */
    struct ovsrec_qos_dscp_map_entry *dscp_map_rows[QOS_DSCP_MAP_ENTRY_COUNT];
    int i;
    for (i = 0; i < QOS_DSCP_MAP_ENTRY_COUNT; i++) {
        struct ovsrec_qos_dscp_map_entry *dscp_map_row =
                ovsrec_qos_dscp_map_entry_insert(txn);
        dscp_map_rows[i] = dscp_map_row;
    }

    /* Update the dscp-map rows. */
    qos_init_default_dscp_map(dscp_map_rows);

    /* Update the system row. */
    struct ovsrec_qos_dscp_map_entry **value_list = xmalloc(
            sizeof *system_row->qos_dscp_map_entries *
            QOS_DSCP_MAP_ENTRY_COUNT);
    for (i = 0; i < QOS_DSCP_MAP_ENTRY_COUNT; i++) {
        value_list[i] = dscp_map_rows[i];
    }
    ovsrec_system_set_qos_dscp_map_entries(system_row, value_list,
            QOS_DSCP_MAP_ENTRY_COUNT);
    free(value_list);
}

void qos_init_queue_profile(struct ovsdb_idl_txn *txn,
                           struct ovsrec_system *system_row) {
    /* Create the default profile. */
    qos_queue_profile_create_factory_default(txn, QOS_DEFAULT_NAME);

    struct ovsrec_q_profile *default_profile = qos_get_queue_profile_row(
            QOS_DEFAULT_NAME);
    if (default_profile == NULL) {
        VLOG_ERR("Profile cannot be NULL.");
        return;
    }

    /* Update the system row to point to the default profile. */
    ovsrec_system_set_q_profile(system_row, default_profile);

    /* Also, create a profile named factory default that is immutable. */
    qos_queue_profile_create_factory_default(txn, QOS_FACTORY_DEFAULT_NAME);

    struct ovsrec_q_profile *factory_default_profile =
            qos_get_queue_profile_row(QOS_FACTORY_DEFAULT_NAME);
    if (factory_default_profile == NULL) {
        VLOG_ERR("Profile cannot be NULL.");
        return;
    }

    /* Set hw_default for profile row. */
    bool hw_default = true;
    ovsrec_q_profile_set_hw_default(factory_default_profile, &hw_default, 1);

    /* Set hw_default for profile entry rows. */
    int i;
    for (i = 0; i < factory_default_profile->n_q_profile_entries; i++) {
        struct ovsrec_q_profile_entry *entry =
                factory_default_profile->value_q_profile_entries[i];
        ovsrec_q_profile_entry_set_hw_default(entry, &hw_default, 1);
    }
}

void qos_init_schedule_profile(struct ovsdb_idl_txn *txn,
                           struct ovsrec_system *system_row) {
    /* Create the default profile. */
    qos_schedule_profile_create_factory_default(txn, QOS_DEFAULT_NAME);

    struct ovsrec_qos *default_profile = qos_get_schedule_profile_row(
            QOS_DEFAULT_NAME);
    if (default_profile == NULL) {
        VLOG_ERR("Profile cannot be NULL.");
        return;
    }

    /* Update the system row to point to the default profile. */
    ovsrec_system_set_qos(system_row, default_profile);

    /* Also, create a profile named factory default that is immutable. */
    qos_schedule_profile_create_factory_default(txn, QOS_FACTORY_DEFAULT_NAME);

    struct ovsrec_qos *factory_default_profile =
            qos_get_schedule_profile_row(QOS_FACTORY_DEFAULT_NAME);
    if (factory_default_profile == NULL) {
        VLOG_ERR("Profile cannot be NULL.");
        return;
    }

    /* Set hw_default for profile row. */
    bool hw_default = true;
    ovsrec_qos_set_hw_default(factory_default_profile, &hw_default, 1);

    /* Set hw_default for profile entry rows. */
    int i;
    for (i = 0; i < factory_default_profile->n_queues; i++) {
        struct ovsrec_queue *entry =
                factory_default_profile->value_queues[i];
        ovsrec_queue_set_hw_default(entry, &hw_default, 1);
    }
}
