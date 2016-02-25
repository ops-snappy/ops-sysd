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

void qos_init_cos_map(struct ovsdb_idl_txn *txn,
                           struct ovsrec_system *system_row) {
    struct ovsrec_qos_cos_map_entry *default_cos_map = qos_create_default_cos_map();

    /* Create the cos-map rows. */
    struct ovsrec_qos_cos_map_entry *cos_map_rows[QOS_COS_MAP_ENTRY_COUNT];
    int i;
    for (i = 0; i < QOS_COS_MAP_ENTRY_COUNT; i++) {
        struct ovsrec_qos_cos_map_entry *cos_map_row = ovsrec_qos_cos_map_entry_insert(
                txn);
        ovsrec_qos_cos_map_entry_set_code_point(cos_map_row,
                default_cos_map[i].code_point);
        ovsrec_qos_cos_map_entry_set_local_priority(cos_map_row,
                default_cos_map[i].local_priority);
        ovsrec_qos_cos_map_entry_set_color(cos_map_row,
                default_cos_map[i].color);
        ovsrec_qos_cos_map_entry_set_description(cos_map_row,
                default_cos_map[i].description);

        cos_map_rows[i] = cos_map_row;
    }

    /* Update the system row. */
    struct ovsrec_qos_cos_map_entry **value_list = xmalloc(
            sizeof *system_row->qos_cos_map_entries * QOS_COS_MAP_ENTRY_COUNT);
    for (i = 0; i < QOS_COS_MAP_ENTRY_COUNT; i++) {
        value_list[i] = cos_map_rows[i];
    }
    ovsrec_system_set_qos_cos_map_entries(system_row, value_list,
            QOS_COS_MAP_ENTRY_COUNT);
    free(value_list);

    qos_destroy_default_cos_map(default_cos_map);
}

void qos_init_dscp_map(struct ovsdb_idl_txn *txn,
                           struct ovsrec_system *system_row) {
    struct ovsrec_qos_dscp_map_entry *default_dscp_map =
            qos_create_default_dscp_map();

    /* Create the dscp-map rows. */
    struct ovsrec_qos_dscp_map_entry *dscp_map_rows[QOS_DSCP_MAP_ENTRY_COUNT];
    int i;
    for (i = 0; i < QOS_DSCP_MAP_ENTRY_COUNT; i++) {
        struct ovsrec_qos_dscp_map_entry *dscp_map_row = ovsrec_qos_dscp_map_entry_insert(
                txn);
        ovsrec_qos_dscp_map_entry_set_code_point(dscp_map_row,
                default_dscp_map[i].code_point);
        ovsrec_qos_dscp_map_entry_set_local_priority(dscp_map_row,
                default_dscp_map[i].local_priority);
        ovsrec_qos_dscp_map_entry_set_priority_code_point(dscp_map_row,
                default_dscp_map[i].priority_code_point, 1);
        ovsrec_qos_dscp_map_entry_set_color(dscp_map_row,
                default_dscp_map[i].color);
        ovsrec_qos_dscp_map_entry_set_description(dscp_map_row,
                default_dscp_map[i].description);

        dscp_map_rows[i] = dscp_map_row;
    }

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

    qos_destroy_default_dscp_map(default_dscp_map);
}

void qos_init_queue_profile(struct ovsdb_idl_txn *txn,
                           struct ovsrec_system *system_row) {
    qos_queue_profile_create_factory_default(txn, QOS_DEFAULT_NAME);

    struct ovsrec_q_profile *profile = qos_get_queue_profile_row(
            QOS_DEFAULT_NAME);
    if (profile == NULL) {
        VLOG_ERR("Profile cannot be NULL.");
        return;
    }

    /* Update the system row. */
    ovsrec_system_set_q_profile(system_row, profile);
}

void qos_init_schedule_profile(struct ovsdb_idl_txn *txn,
                           struct ovsrec_system *system_row) {
    qos_schedule_profile_create_factory_default(txn, QOS_DEFAULT_NAME);

    struct ovsrec_qos *profile = qos_get_schedule_profile_row(
            QOS_DEFAULT_NAME);
    if (profile == NULL) {
        VLOG_ERR("Profile cannot be NULL.");
        return;
    }

    /* Update the system row. */
    ovsrec_system_set_qos(system_row, profile);
}
