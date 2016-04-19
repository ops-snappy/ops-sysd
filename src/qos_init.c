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

#include <config.h>

#include "qos_init.h"

#include <ops-utils.h>
#include <stdlib.h>
#include <sys/types.h>

#include "config-yaml.h"
#include "sysd_cfg_yaml.h"
#include "sysd_qos_utils.h"
#include "smap.h"
#include "util.h"
#include "vswitch-idl.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(qos_init);

/**
 * If defined, then the dscp map cos remark capability will be disabled.
 */
#define QOS_CAPABILITY_DSCP_MAP_COS_REMARK_DISABLED

struct ovsdb_idl *idl;

/**
 * Returns true if the givien queue_row contains the local_priority.
 */
static bool
has_local_priority(struct ovsrec_q_profile_entry *queue_row,
                   int64_t local_priority)
{
    int i;
    for (i = 0; i < queue_row->n_local_priorities; i++) {
        if (queue_row->local_priorities[i] == local_priority) {
            return true;
        }
    }

    return false;
}

/**
 * Returns the queue_profile_row for the given profile_name.
 */
static struct ovsrec_q_profile *
qos_get_queue_profile_row(const char *profile_name)
{
    const struct ovsrec_q_profile *profile_row;
    OVSREC_Q_PROFILE_FOR_EACH(profile_row, idl) {
        if (strcmp(profile_row->name, profile_name) == 0) {
            return (struct ovsrec_q_profile *)profile_row;
        }
    }

    return NULL;
}

/**
 * Returns the queue_profile_entry_row for the given profile_row and
 * queue_num.
 */
static struct ovsrec_q_profile_entry *
qos_get_queue_profile_entry_row(struct ovsrec_q_profile *profile_row,
                                int64_t queue_num)
{
    int i;
    for (i = 0; i < profile_row->n_q_profile_entries; i++) {
        if (profile_row->key_q_profile_entries[i] == queue_num) {
            return profile_row->value_q_profile_entries[i];
        }
    }

    return NULL;
}

/**
 * Inserts into the database and returns a new queue_profile_entry_row
 * for the given profile_row, queue_num, and txn.
 */
static struct ovsrec_q_profile_entry *
insert_queue_profile_queue_row(struct ovsrec_q_profile *profile_row,
                               int64_t queue_num,
                               struct ovsdb_idl_txn *txn)
{
    /* Create the queue row. */
    struct ovsrec_q_profile_entry *queue_row =
        ovsrec_q_profile_entry_insert(txn);

    /* Update the profile row. */
    int64_t *key_list =
        xmalloc(sizeof(int64_t) *
                (profile_row->n_q_profile_entries + 1));
    struct ovsrec_q_profile_entry **value_list =
        xmalloc(sizeof*profile_row->value_q_profile_entries *
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

/**
 * Creates a queue_profile row in Q_Profile for the txn and profile_name.
 */
void
qos_queue_profile_command(struct ovsdb_idl_txn *txn,
                          const char *profile_name)
{
    /* Retrieve the row. */
    struct ovsrec_q_profile *profile_row =
        qos_get_queue_profile_row(profile_name);
    if (profile_row == NULL) {
        /* Create a new row. */
        profile_row = ovsrec_q_profile_insert(txn);
        ovsrec_q_profile_set_name(profile_row, profile_name);
    }

    return;
}

/**
 * Adds the given local_priority to the queue_row.
 */
static void
add_local_priority(struct ovsrec_q_profile_entry *queue_row,
                   int64_t local_priority)
{
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

/**
 * Executes the queue_profile_map_command for the given txn, profile_name,
 * queue_num, and local_priority. Returns false if an error occurred.
 */
static bool
qos_queue_profile_row_command(struct ovsdb_idl_txn *txn,
                              const char *profile_name,
                              int64_t queue_num, int64_t local_priority,
                              const char *description)
{
    /* Retrieve the profile row. */
    struct ovsrec_q_profile *profile_row =
        qos_get_queue_profile_row(profile_name);
    if (profile_row == NULL) {
        VLOG_ERR("Profile %s does not exist.",
                 profile_name);
        return false;
    }

    /* Retrieve the existing queue row. */
    struct ovsrec_q_profile_entry *queue_row =
        qos_get_queue_profile_entry_row(profile_row, queue_num);

    /* If no existing row, then insert a new queue row. */
    if (queue_row == NULL) {
        queue_row = insert_queue_profile_queue_row(
                profile_row, queue_num, txn);
    }

    /* Update the queue row. */
    VLOG_INFO(".... updating %p", queue_row);
    add_local_priority(queue_row, local_priority);
    if (description != NULL) {
        ovsrec_q_profile_entry_set_description(queue_row, description);
    }

    return true;
}

/**
 * Creates the factory default queue profile for the given txn and
 * default_name.
 */
static void
qos_queue_profile_create_factory_default(struct ovsdb_idl_txn *txn,
                                         const char *default_name)
{
    unsigned int ii;
    const YamlQueueProfileEntry *yaml_queue_profile_entry;
    int count;

    qos_queue_profile_command(txn, default_name);

    count = sysd_cfg_yaml_get_queue_profile_entry_count();
    VLOG_DBG("THERE ARE %d QUEUE_PROFILE ENTRIES", count);
    for (ii = 0; ii < count; ii++) {
        /* pointer could be NULL only if YAML init has failed. */
        yaml_queue_profile_entry = sysd_cfg_yaml_get_queue_profile_entry(ii);
        if (yaml_queue_profile_entry) {
            VLOG_INFO(".. queue %d pri %d", yaml_queue_profile_entry->queue,
                      yaml_queue_profile_entry->local_priority);
            qos_queue_profile_row_command(txn, default_name,
                    yaml_queue_profile_entry->queue,
                    yaml_queue_profile_entry->local_priority,
                    yaml_queue_profile_entry->description);
        }
    }
}

/**
 * Returns the schedule_profile_row for the given profile_name.
 */
static struct ovsrec_qos *
qos_get_schedule_profile_row(const char *profile_name)
{
    const struct ovsrec_qos *profile_row;
    OVSREC_QOS_FOR_EACH(profile_row, idl) {
        if (strcmp(profile_row->name, profile_name) == 0) {
            return (struct ovsrec_qos *)profile_row;
        }
    }

    return NULL;
}

/**
 * Returns the schedule_profile_entry_row for the given profile_row and
 * queue_num.
 */
static struct ovsrec_queue *
qos_get_schedule_profile_entry_row(struct ovsrec_qos *profile_row,
                                   int64_t queue_num)
{
    int i;
    for (i = 0; i < profile_row->n_queues; i++) {
        if (profile_row->key_queues[i] == queue_num) {
            return profile_row->value_queues[i];
        }
    }

    return NULL;
}

/**
 * Inserts into the database and returns the schedule_profile_row for the
 * given profile_row, queue_num, and txn.
 */
static struct ovsrec_queue *
insert_schedule_profile_queue_row(struct ovsrec_qos *profile_row,
                                  int64_t queue_num,
                                  struct ovsdb_idl_txn *txn)
{
    /* Create the queue row. */
    struct ovsrec_queue *queue_row =
        ovsrec_queue_insert(txn);

    /* Update the profile row. */
    int64_t *key_list =
        xmalloc(sizeof(int64_t) *
                (profile_row->n_queues + 1));
    struct ovsrec_queue **value_list =
        xmalloc(sizeof*profile_row->value_queues *
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

/**
 * Executes the schedule_profile_command for the given txn, and profile_name.
 */
void
qos_schedule_profile_command(struct ovsdb_idl_txn *txn,
                             const char *profile_name)
{
    /* Retrieve the row. */
    struct ovsrec_qos *profile_row =
        qos_get_schedule_profile_row(profile_name);
    if (profile_row == NULL) {
        /* Create a new row. */
        profile_row = ovsrec_qos_insert(txn);
        ovsrec_qos_set_name(profile_row, profile_name);
    }

    return;
}

/**
 * Creates a schedule-profile row for the given txn, profile_name,
 * queue_num, algorithm and weight. Returns false if an error occurred.
 */
static bool
qos_schedule_profile_row_command(struct ovsdb_idl_txn *txn,
                                 const char *profile_name,
                                 int64_t queue_num,
                                 char * algorithm,
                                 int64_t weight)
{
    /* Retrieve the profile row. */
    struct ovsrec_qos *profile_row =
        qos_get_schedule_profile_row(profile_name);
    if (profile_row == NULL) {
        VLOG_ERR("Profile %s does not exist.",
                 profile_name);
        return false;
    }

    /* Retrieve the existing queue row. */
    struct ovsrec_queue *queue_row =
        qos_get_schedule_profile_entry_row(profile_row, queue_num);

    /* If no existing row, then insert a new queue row. */
    if (queue_row == NULL) {
        queue_row = insert_schedule_profile_queue_row(
                profile_row, queue_num, txn);
    }

    ovsrec_queue_set_algorithm(queue_row, algorithm);
    /* TODO: can "strict" have weight set to 0? */
    if ( ! strcmp(algorithm, OVSREC_QUEUE_ALGORITHM_STRICT)) {
        ovsrec_queue_set_weight(queue_row, NULL, 0);
    }
    else {
        ovsrec_queue_set_weight(queue_row, &weight, 1);
    }

    return true;
}

/**
 * Creates the factory default schedule profile for the given txn and
 * default_name.
 */
static void
qos_schedule_profile_create_factory_default(struct ovsdb_idl_txn *txn,
                                            const char *default_name)
{
    struct ovsrec_qos *profile_row;
    int count = sysd_cfg_yaml_get_schedule_profile_entry_count();
    const YamlScheduleProfileEntry *yaml_schedule_profile_entry;

    VLOG_DBG("THERE ARE %d SCHEDULE_PROFILE ENTRIES", count);

    qos_schedule_profile_command(txn, default_name);

    /* Delete all queue rows. */
    profile_row = qos_get_schedule_profile_row(default_name);
    ovsrec_qos_set_queues(profile_row, NULL, NULL, 0);

    /* Create all queue rows. */
    for (int ii = 0; ii < count; ii++) {
        yaml_schedule_profile_entry =
            sysd_cfg_yaml_get_schedule_profile_entry(ii);
        /* pointer could be NULL only if YAML init has failed. */
        if (yaml_schedule_profile_entry) {
            qos_schedule_profile_row_command(txn, default_name,
                    yaml_schedule_profile_entry->queue,
                    yaml_schedule_profile_entry->algorithm,
                    yaml_schedule_profile_entry->weight);
        }
    }
}

/**
 * Initializes qos trust for the given txn and system_row.
 */
void
qos_init_trust(struct ovsdb_idl_txn *txn,
               struct ovsrec_system *system_row)
{
    YamlQosInfo *qos_info;
    struct smap smap;

    qos_info = sysd_cfg_yaml_get_qos_info();
    if (qos_info == NULL) {
        return;
    }

    /* trust pointer could be NULL only if YAML init has failed. */
    if (qos_info->trust) {
        smap_clone(&smap, &system_row->qos_config);
        smap_replace(&smap, QOS_TRUST_KEY, qos_info->trust);
        ovsrec_system_set_qos_config(system_row, &smap);
        smap_destroy(&smap);
    }
    return;
}

/**
 * Sets the given cos_map_entry, code_point, local_priority, color, and
 * description for the given cos_map_entry.
 */
static void
set_cos_map_entry(struct ovsrec_qos_cos_map_entry *cos_map_entry,
                  int64_t code_point, int64_t local_priority,
                  char *color, char *description)
{
    /* Initialize the actual config. */
    ovsrec_qos_cos_map_entry_set_code_point(cos_map_entry, code_point);
    ovsrec_qos_cos_map_entry_set_local_priority(cos_map_entry, local_priority);
    ovsrec_qos_cos_map_entry_set_color(cos_map_entry, color);
    ovsrec_qos_cos_map_entry_set_description(cos_map_entry, description);

    char code_point_buffer[QOS_CLI_STRING_BUFFER_SIZE];
    snprintf(code_point_buffer, QOS_CLI_STRING_BUFFER_SIZE,
             "%" PRId64, code_point);
    char local_priority_buffer[QOS_CLI_STRING_BUFFER_SIZE];
    snprintf(local_priority_buffer, QOS_CLI_STRING_BUFFER_SIZE,
             "%" PRId64, local_priority);

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

/**
 * Initializes the given default cos_map.
 */
static void
qos_init_default_cos_map(struct ovsrec_qos_cos_map_entry **cos_map)
{
    const YamlCosMapEntry *yaml_cos_map_entry;
    int count;

    count = sysd_cfg_yaml_get_cos_map_entry_count();
    VLOG_DBG("THERE ARE %d COS MAP ENTRIES", count);

    /* Initialize the cos-map entry data. */
    unsigned int ii;
    for (ii = 0; ii < count; ii++) {
        yaml_cos_map_entry = sysd_cfg_yaml_get_cos_map_entry(ii);
        set_cos_map_entry(cos_map[ii],
                          yaml_cos_map_entry->code_point,
                          yaml_cos_map_entry->local_priority,
                          yaml_cos_map_entry->color,
                          yaml_cos_map_entry->description);
    }
}

/**
 * Creates cos_map rows and initializes them for the given txn and
 * system_row.
 */
void
qos_init_cos_map(struct ovsdb_idl_txn *txn,
                 struct ovsrec_system *system_row)
{
    const YamlCosMapEntry *yaml_cos_map_entry =
            sysd_cfg_yaml_get_cos_map_entry(0);
    if (yaml_cos_map_entry == NULL) {
        return;
    }

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
        sizeof*system_row->qos_cos_map_entries *
        QOS_COS_MAP_ENTRY_COUNT);
    for (i = 0; i < QOS_COS_MAP_ENTRY_COUNT; i++) {
        value_list[i] = cos_map_rows[i];
    }
    ovsrec_system_set_qos_cos_map_entries(system_row, value_list,
                                          QOS_COS_MAP_ENTRY_COUNT);
    free(value_list);
}

/**
 * Sets the given dscp_map_entry, code_point, local_priority, color, and
 * description for the given dscp_map_entry.
 */
static void
set_dscp_map_entry(struct ovsrec_qos_dscp_map_entry *dscp_map_entry,
                   int64_t code_point, int64_t local_priority,
#ifdef QOS_CAPABILITY_DSCP_MAP_COS_REMARK_DISABLED
    /* Disabled for dill. */
#else
                   int64_t priority_code_point,
#endif
                   char *color, char *description)
{
    /* Initialize the actual config. */
    ovsrec_qos_dscp_map_entry_set_code_point(dscp_map_entry, code_point);
    ovsrec_qos_dscp_map_entry_set_local_priority(dscp_map_entry,
                                                 local_priority);
#ifdef QOS_CAPABILITY_DSCP_MAP_COS_REMARK_DISABLED
    /* Disabled for dill. */
#else
    ovsrec_qos_dscp_map_entry_set_priority_code_point(dscp_map_entry,
                                                      &priority_code_point, 1);
#endif
    ovsrec_qos_dscp_map_entry_set_color(dscp_map_entry, color);
    ovsrec_qos_dscp_map_entry_set_description(dscp_map_entry, description);

    char code_point_buffer[QOS_CLI_STRING_BUFFER_SIZE];
    snprintf(code_point_buffer, QOS_CLI_STRING_BUFFER_SIZE,
             "%" PRId64, code_point);
    char local_priority_buffer[QOS_CLI_STRING_BUFFER_SIZE];
    snprintf(local_priority_buffer, QOS_CLI_STRING_BUFFER_SIZE,
             "%" PRId64, local_priority);
#ifdef QOS_CAPABILITY_DSCP_MAP_COS_REMARK_DISABLED
    /* Disabled for dill. */
#else
    char priority_code_point_buffer[QOS_CLI_STRING_BUFFER_SIZE];
    snprintf(priority_code_point_buffer, QOS_CLI_STRING_BUFFER_SIZE,
             "%" PRId64, priority_code_point);
#endif

    /* Save the factory defaults so they can be restored later. */
    struct smap smap;
    smap_clone(&smap, &dscp_map_entry->hw_defaults);
    smap_replace(&smap, QOS_DEFAULT_CODE_POINT_KEY, code_point_buffer);
    smap_replace(&smap, QOS_DEFAULT_LOCAL_PRIORITY_KEY,
                 local_priority_buffer);
#ifdef QOS_CAPABILITY_DSCP_MAP_COS_REMARK_DISABLED
    /* Disabled for dill. */
#else
    smap_replace(&smap, QOS_DEFAULT_PRIORITY_CODE_POINT_KEY,
                 priority_code_point_buffer);
#endif
    smap_replace(&smap, QOS_DEFAULT_COLOR_KEY, color);
    smap_replace(&smap, QOS_DEFAULT_DESCRIPTION_KEY, description);
    ovsrec_qos_dscp_map_entry_set_hw_defaults(dscp_map_entry, &smap);
    smap_destroy(&smap);
}

/**
 * Initializes the given default dscp_map.
 */
static void
qos_init_default_dscp_map(
        struct ovsrec_qos_dscp_map_entry **dscp_map)
{
    const YamlDscpMapEntry *yaml_dscp_map_entry;
    int count;

    count = sysd_cfg_yaml_get_dscp_map_entry_count();
    VLOG_DBG("THERE ARE %d DSCP MAP ENTRIES", count);

    /* Initialize the dscp-map entry data. */
    unsigned int ii;
    for (ii = 0; ii < count; ii++) {
        yaml_dscp_map_entry = sysd_cfg_yaml_get_dscp_map_entry(ii);
        set_dscp_map_entry(dscp_map[ii],
                           yaml_dscp_map_entry->code_point,
                           yaml_dscp_map_entry->local_priority,
#ifdef QOS_CAPABILITY_DSCP_MAP_COS_REMARK_DISABLED
    /* Disabled for dill. */
#else
                           yaml_dscp_map_entry->priority_code_point,
#endif
                           yaml_dscp_map_entry->color,
                           yaml_dscp_map_entry->description);
    }
}

/**
 * Creates and initializes the default dscp map for the given txn and
 * system_row.
 */
void
qos_init_dscp_map(struct ovsdb_idl_txn *txn,
                  struct ovsrec_system *system_row)
{
    const YamlDscpMapEntry *yaml_dscp_map_entry =
            sysd_cfg_yaml_get_dscp_map_entry(0);
    if (yaml_dscp_map_entry == NULL) {
        return;
    }

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
        sizeof*system_row->qos_dscp_map_entries *
        QOS_DSCP_MAP_ENTRY_COUNT);
    for (i = 0; i < QOS_DSCP_MAP_ENTRY_COUNT; i++) {
        value_list[i] = dscp_map_rows[i];
    }
    ovsrec_system_set_qos_dscp_map_entries(system_row, value_list,
                                           QOS_DSCP_MAP_ENTRY_COUNT);
    free(value_list);
}

/**
 * Initializes the queue_profile for the given txn and system_row.
 */
void
qos_init_queue_profile(struct ovsdb_idl_txn *txn,
                       struct ovsrec_system *system_row)
{
    YamlQosInfo *qos_info = sysd_cfg_yaml_get_qos_info();
    if (qos_info == NULL) {
        return;
    }

    /* Create the default profile. */
    qos_queue_profile_create_factory_default(txn, qos_info->default_name);

    struct ovsrec_q_profile *default_profile =
                    qos_get_queue_profile_row(qos_info->default_name);
    if (default_profile == NULL) {
        VLOG_ERR("Profile cannot be NULL.");
        return;
    }

    /* Update the system row to point to the default profile. */
    ovsrec_system_set_q_profile(system_row, default_profile);

    /* Also, create a profile named factory default that is immutable. */
    qos_queue_profile_create_factory_default(txn,
                                             qos_info->factory_default_name);

    struct ovsrec_q_profile *factory_default_profile =
                    qos_get_queue_profile_row(qos_info->factory_default_name);
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

/**
 * Initializes the schedule_profile for the given txn and system_row.
 */
void
qos_init_schedule_profile(struct ovsdb_idl_txn *txn,
                           struct ovsrec_system *system_row)
{
    YamlQosInfo *qos_info = sysd_cfg_yaml_get_qos_info();
    if (qos_info == NULL) {
        return;
    }

    /* Create the default profile. */
    qos_schedule_profile_create_factory_default(txn, qos_info->default_name);

    struct ovsrec_qos *default_profile =
        qos_get_schedule_profile_row(qos_info->default_name);
    if (default_profile == NULL) {
        VLOG_ERR("Profile cannot be NULL.");
        return;
    }

    /* Update the system row to point to the default profile. */
    ovsrec_system_set_qos(system_row, default_profile);

    /* Also, create a profile named factory default that is immutable. */
    qos_schedule_profile_create_factory_default(txn,
            qos_info->factory_default_name);

    struct ovsrec_qos *factory_default_profile =
        qos_get_schedule_profile_row(qos_info->factory_default_name);
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
