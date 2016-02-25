/****************************************************************************
 * (c) Copyright 2016 Hewlett Packard Enterprise Development LP
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

#include <pwd.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <setjmp.h>
#include <sys/un.h>
#include <sys/wait.h>

#include "openswitch-idl.h"
#include "openvswitch/vlog.h"
#include "ovsdb-idl.h"
#include "sysd_qos_utils.h"
#include "smap.h"
#include "vswitch-idl.h"

static void set_dscp_map_entry(struct ovsrec_qos_dscp_map_entry *dscp_map_entry,
        int64_t code_point, int64_t local_priority,
        int64_t priority_code_point, char *color, char *description) {
    dscp_map_entry->code_point = code_point;
    dscp_map_entry->local_priority = local_priority;
    *dscp_map_entry->priority_code_point = priority_code_point;
    dscp_map_entry->color = color;
    dscp_map_entry->description = description;
}

struct ovsrec_qos_dscp_map_entry *qos_create_default_dscp_map(void) {
    struct ovsrec_qos_dscp_map_entry *dscp_map = xmalloc(
            sizeof(struct ovsrec_qos_dscp_map_entry) * QOS_DSCP_MAP_ENTRY_COUNT);

    /* Allocate each priority_code_point field. */
    int code_point;
    for (code_point = 0; code_point < QOS_DSCP_MAP_ENTRY_COUNT; code_point++) {
        dscp_map[code_point].priority_code_point = xmalloc(sizeof(int64_t));
    }

    set_dscp_map_entry(&dscp_map[0], 0, 0, 1, "green", "CS0");
    set_dscp_map_entry(&dscp_map[1], 1, 0, 1, "green", "");
    set_dscp_map_entry(&dscp_map[2], 2, 0, 1, "green", "");
    set_dscp_map_entry(&dscp_map[3], 3, 0, 1, "green", "");
    set_dscp_map_entry(&dscp_map[4], 4, 0, 1, "green", "");
    set_dscp_map_entry(&dscp_map[5], 5, 0, 1, "green", "");
    set_dscp_map_entry(&dscp_map[6], 6, 0, 1, "green", "");
    set_dscp_map_entry(&dscp_map[7], 7, 0, 1, "green", "");
    set_dscp_map_entry(&dscp_map[8], 8, 1, 0, "green", "CS1");
    set_dscp_map_entry(&dscp_map[9], 9, 1, 0, "green", "");

    set_dscp_map_entry(&dscp_map[10], 10, 1, 0, "green", "AF11");
    set_dscp_map_entry(&dscp_map[11], 11, 1, 0, "green", "");
    set_dscp_map_entry(&dscp_map[12], 12, 1, 0, "yellow", "AF12");
    set_dscp_map_entry(&dscp_map[13], 13, 1, 0, "green", "");
    set_dscp_map_entry(&dscp_map[14], 14, 1, 0, "red", "AF13");
    set_dscp_map_entry(&dscp_map[15], 15, 1, 0, "green", "");
    set_dscp_map_entry(&dscp_map[16], 16, 2, 2, "green", "CS2");
    set_dscp_map_entry(&dscp_map[17], 17, 2, 2, "green", "");
    set_dscp_map_entry(&dscp_map[18], 18, 2, 2, "green", "AF21");
    set_dscp_map_entry(&dscp_map[19], 19, 2, 2, "green", "");

    set_dscp_map_entry(&dscp_map[20], 20, 2, 2, "yellow", "AF22");
    set_dscp_map_entry(&dscp_map[21], 21, 2, 2, "green", "");
    set_dscp_map_entry(&dscp_map[22], 22, 2, 2, "red", "AF23");
    set_dscp_map_entry(&dscp_map[23], 23, 2, 2, "green", "");
    set_dscp_map_entry(&dscp_map[24], 24, 3, 3, "green", "CS3");
    set_dscp_map_entry(&dscp_map[25], 25, 3, 3, "green", "");
    set_dscp_map_entry(&dscp_map[26], 26, 3, 3, "green", "AF31");
    set_dscp_map_entry(&dscp_map[27], 27, 3, 3, "green", "");
    set_dscp_map_entry(&dscp_map[28], 28, 3, 3, "yellow", "AF32");
    set_dscp_map_entry(&dscp_map[29], 29, 3, 3, "green", "");

    set_dscp_map_entry(&dscp_map[30], 30, 3, 3, "red", "AF33");
    set_dscp_map_entry(&dscp_map[31], 31, 3, 3, "green", "");
    set_dscp_map_entry(&dscp_map[32], 32, 4, 4, "green", "CS4");
    set_dscp_map_entry(&dscp_map[33], 33, 4, 4, "green", "");
    set_dscp_map_entry(&dscp_map[34], 34, 4, 4, "green", "AF41");
    set_dscp_map_entry(&dscp_map[35], 35, 4, 4, "green", "");
    set_dscp_map_entry(&dscp_map[36], 36, 4, 4, "yellow", "AF42");
    set_dscp_map_entry(&dscp_map[37], 37, 4, 4, "green", "");
    set_dscp_map_entry(&dscp_map[38], 38, 4, 4, "red", "AF43");
    set_dscp_map_entry(&dscp_map[39], 39, 4, 4, "green", "");

    set_dscp_map_entry(&dscp_map[40], 40, 5, 5, "green", "CS5");
    set_dscp_map_entry(&dscp_map[41], 41, 5, 5, "green", "");
    set_dscp_map_entry(&dscp_map[42], 42, 5, 5, "green", "");
    set_dscp_map_entry(&dscp_map[43], 43, 5, 5, "green", "");
    set_dscp_map_entry(&dscp_map[44], 44, 5, 5, "green", "");
    set_dscp_map_entry(&dscp_map[45], 45, 5, 5, "green", "");
    set_dscp_map_entry(&dscp_map[46], 46, 5, 5, "green", "EF");
    set_dscp_map_entry(&dscp_map[47], 47, 5, 5, "green", "");
    set_dscp_map_entry(&dscp_map[48], 48, 6, 6, "green", "CS6");
    set_dscp_map_entry(&dscp_map[49], 49, 6, 6, "green", "");

    set_dscp_map_entry(&dscp_map[50], 50, 6, 6, "green", "");
    set_dscp_map_entry(&dscp_map[51], 51, 6, 6, "green", "");
    set_dscp_map_entry(&dscp_map[52], 52, 6, 6, "green", "");
    set_dscp_map_entry(&dscp_map[53], 53, 6, 6, "green", "");
    set_dscp_map_entry(&dscp_map[54], 54, 6, 6, "green", "");
    set_dscp_map_entry(&dscp_map[55], 55, 6, 6, "green", "");
    set_dscp_map_entry(&dscp_map[56], 56, 7, 7, "green", "CS7");
    set_dscp_map_entry(&dscp_map[57], 57, 7, 7, "green", "");
    set_dscp_map_entry(&dscp_map[58], 58, 7, 7, "green", "");
    set_dscp_map_entry(&dscp_map[59], 59, 7, 7, "green", "");

    set_dscp_map_entry(&dscp_map[60], 60, 7, 7, "green", "");
    set_dscp_map_entry(&dscp_map[61], 61, 7, 7, "green", "");
    set_dscp_map_entry(&dscp_map[62], 62, 7, 7, "green", "");
    set_dscp_map_entry(&dscp_map[63], 63, 7, 7, "green", "");

    return dscp_map;
}

void qos_destroy_default_dscp_map(struct ovsrec_qos_dscp_map_entry *dscp_map) {
    if (dscp_map == NULL) {
        return;
    }

    /* Free each priority_code_point field. */
    int code_point;
    for (code_point = 0; code_point < QOS_DSCP_MAP_ENTRY_COUNT; code_point++) {
        free(dscp_map[code_point].priority_code_point);
    }

    free(dscp_map);
}

static void set_cos_map_entry(struct ovsrec_qos_cos_map_entry *cos_map_entry,
        int64_t code_point, int64_t local_priority, char *color,
        char *description) {
    cos_map_entry->code_point = code_point;
    cos_map_entry->local_priority = local_priority;
    cos_map_entry->color = color;
    cos_map_entry->description = description;
}

struct ovsrec_qos_cos_map_entry *qos_create_default_cos_map(void) {
    struct ovsrec_qos_cos_map_entry *cos_map = xmalloc(
            sizeof(struct ovsrec_qos_cos_map_entry) * QOS_COS_MAP_ENTRY_COUNT);
    set_cos_map_entry(&cos_map[0], 0, 1, "green", "Best_Effort");
    set_cos_map_entry(&cos_map[1], 1, 0, "green", "Background");
    set_cos_map_entry(&cos_map[2], 2, 2, "green", "Excellent_Effort");
    set_cos_map_entry(&cos_map[3], 3, 3, "green", "Critical_Applications");
    set_cos_map_entry(&cos_map[4], 4, 4, "green", "Video");
    set_cos_map_entry(&cos_map[5], 5, 5, "green", "Voice");
    set_cos_map_entry(&cos_map[6], 6, 6, "green", "Internetwork_Control");
    set_cos_map_entry(&cos_map[7], 7, 7, "green", "Network_Control");

    return cos_map;
}

void qos_destroy_default_cos_map(struct ovsrec_qos_cos_map_entry *cos_map) {
    if (cos_map == NULL) {
        return;
    }

    free(cos_map);
}
