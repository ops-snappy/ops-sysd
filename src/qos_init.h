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

#ifndef _QOS_INIT_H_
#define _QOS_INIT_H_

#include <config-yaml.h>
#include <ops-utils.h>
#include <stdlib.h>
#include <sys/types.h>

#include <smap.h>
#include <util.h>
#include <vswitch-idl.h>

/**
 * Initializes factory default qos trust settings in ovsdb.
 */
void qos_init_trust(struct ovsdb_idl_txn *txn,
        struct ovsrec_system *system_row);

/**
 * Initializes factory default qos cos map settings in ovsdb.
 */
void qos_init_cos_map(struct ovsdb_idl_txn *txn,
        struct ovsrec_system *system_row);

/**
 * Initializes factory default qos dscp map settings in ovsdb.
 */
void qos_init_dscp_map(struct ovsdb_idl_txn *txn,
        struct ovsrec_system *system_row);

/**
 * Initializes factory default qos queue profile settings in ovsdb.
 */
void qos_init_queue_profile(struct ovsdb_idl_txn *txn,
        struct ovsrec_system *system_row);

/**
 * Initializes factory default qos schedule profile settings in ovsdb.
 */
void qos_init_schedule_profile(struct ovsdb_idl_txn *txn,
        struct ovsrec_system *system_row);

#endif /* _QOS_INIT_H_ */
