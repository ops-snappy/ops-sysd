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
