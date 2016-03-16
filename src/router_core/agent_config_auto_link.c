/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <qpid/dispatch/ctools.h>
#include "agent_config_auto_link.h"
#include "route_control.h"
#include <stdio.h>

#define QDR_CONFIG_AUTO_LINK_NAME          0
#define QDR_CONFIG_AUTO_LINK_IDENTITY      1
#define QDR_CONFIG_AUTO_LINK_TYPE          2
#define QDR_CONFIG_AUTO_LINK_ADDR          3
#define QDR_CONFIG_AUTO_LINK_DIR           4
#define QDR_CONFIG_AUTO_LINK_PHASE         5
#define QDR_CONFIG_AUTO_LINK_CONNECTION    6
#define QDR_CONFIG_AUTO_LINK_CONTAINER_ID  7
#define QDR_CONFIG_AUTO_LINK_LINK_REF      8

const char *qdr_config_auto_link_columns[] =
    {"name",
     "identity",
     "type",
     "addr",
     "dir",
     "phase",
     "containerId",
     "connection",
     "linkRef",
     0};


static void qdr_config_auto_link_insert_column_CT(qdr_auto_link_t *al, int col, qd_composed_field_t *body, bool as_map)
{
    const char *text = 0;
    const char *key;
    char id_str[100];

    if (as_map)
        qd_compose_insert_string(body, qdr_config_auto_link_columns[col]);

    switch(col) {
    case QDR_CONFIG_AUTO_LINK_NAME:
        if (al->name)
            qd_compose_insert_string(body, al->name);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_AUTO_LINK_IDENTITY:
        snprintf(id_str, 100, "%ld", al->identity);
        qd_compose_insert_string(body, id_str);
        break;

    case QDR_CONFIG_AUTO_LINK_TYPE:
        qd_compose_insert_string(body, "org.apache.qpid.dispatch.config.autoLink");
        break;

    case QDR_CONFIG_AUTO_LINK_ADDR:
        key = (const char*) qd_hash_key_by_handle(al->addr->hash_handle);
        if (key && key[0] == 'M')
            qd_compose_insert_string(body, &key[1]);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_AUTO_LINK_DIR:
        text = al->dir == QD_INCOMING ? "in" : "out";
        qd_compose_insert_string(body, text);
        break;

    case QDR_CONFIG_AUTO_LINK_PHASE:
        qd_compose_insert_int(body, al->phase);
        break;

    case QDR_CONFIG_AUTO_LINK_CONNECTION:
    case QDR_CONFIG_AUTO_LINK_CONTAINER_ID:
        if (al->conn_id) {
            key = (const char*) qd_hash_key_by_handle(al->conn_id->hash_handle);
            if (key && key[0] == 'L' && col == QDR_CONFIG_AUTO_LINK_CONNECTION) {
                qd_compose_insert_string(body, &key[1]);
                break;
            }
            if (key && key[0] == 'C' && col == QDR_CONFIG_AUTO_LINK_CONTAINER_ID) {
                qd_compose_insert_string(body, &key[1]);
                break;
            }
        }
        qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_AUTO_LINK_LINK_REF:
        if (al->link) {
            snprintf(id_str, 100, "%ld", al->link->identity);
            qd_compose_insert_string(body, id_str);
        } else
            qd_compose_insert_null(body);
    }
}


static void qdr_agent_write_config_auto_link_CT(qdr_query_t *query,  qdr_auto_link_t *al)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);
    int i = 0;
    while (query->columns[i] >= 0) {
        qdr_config_auto_link_insert_column_CT(al, query->columns[i], body, false);
        i++;
    }
    qd_compose_end_list(body);
}


static void qdr_manage_advance_config_auto_link_CT(qdr_query_t *query, qdr_auto_link_t *al)
{
    query->next_offset++;
    al = DEQ_NEXT(al);
    query->more = !!al;
}


void qdra_config_auto_link_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    //
    // Queries that get this far will always succeed.
    //
    query->status = QD_AMQP_OK;

    //
    // If the offset goes beyond the set of objects, end the query now.
    //
    if (offset >= DEQ_SIZE(core->auto_links)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the object at the offset.
    //
    qdr_auto_link_t *al = DEQ_HEAD(core->auto_links);
    for (int i = 0; i < offset && al; i++)
        al = DEQ_NEXT(al);
    assert(al);

    //
    // Write the columns of the object into the response body.
    //
    qdr_agent_write_config_auto_link_CT(query, al);

    //
    // Advance to the next auto_link
    //
    query->next_offset = offset;
    qdr_manage_advance_config_auto_link_CT(query, al);

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_config_auto_link_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_auto_link_t *al = 0;

    if (query->next_offset < DEQ_SIZE(core->auto_links)) {
        al = DEQ_HEAD(core->auto_links);
        for (int i = 0; i < query->next_offset && al; i++)
            al = DEQ_NEXT(al);
    }

    if (al) {
        //
        // Write the columns of the addr entity into the response body.
        //
        qdr_agent_write_config_auto_link_CT(query, al);

        //
        // Advance to the next object
        //
        qdr_manage_advance_config_auto_link_CT(query, al);
    } else
        query->more = false;

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


static const char *qdra_auto_link_direction_CT(qd_parsed_field_t *field, qd_direction_t *dir)
{
    if (field) {
        qd_field_iterator_t *iter = qd_parse_raw(field);
        if (qd_field_iterator_equal(iter, (unsigned char*) "in")) {
            *dir = QD_INCOMING;
            return 0;
        } else if (qd_field_iterator_equal(iter, (unsigned char*) "out")) {
            *dir = QD_OUTGOING;
            return 0;
        }
        return "Invalid value for 'dir'";
    }
    return "Missing value for 'dir'";
}


static qdr_auto_link_t *qdr_auto_link_config_find_by_identity_CT(qdr_core_t *core, qd_field_iterator_t *identity)
{
    if (!identity)
        return 0;

    qdr_auto_link_t *rc = DEQ_HEAD(core->auto_links);
    while (rc) {
        // Convert the passed in identity to a char*
        char id[100];
        snprintf(id, 100, "%ld", rc->identity);
        if (qd_field_iterator_equal(identity, (const unsigned char*) id))
            break;
        rc = DEQ_NEXT(rc);
    }

    return rc;

}


static qdr_auto_link_t *qdr_auto_link_config_find_by_name_CT(qdr_core_t *core, qd_field_iterator_t *name)
{
    if (!name)
        return 0;

    qdr_auto_link_t *rc = DEQ_HEAD(core->auto_links);
    while (rc) { // Sometimes the name can be null
        if (rc->name && qd_field_iterator_equal(name, (const unsigned char*) rc->name))
            break;
        rc = DEQ_NEXT(rc);
    }

    return rc;
}


void qdra_config_auto_link_delete_CT(qdr_core_t          *core,
                                      qdr_query_t         *query,
                                      qd_field_iterator_t *name,
                                      qd_field_iterator_t *identity)
{
    qdr_auto_link_t *al = 0;

    if (!name && !identity)
        query->status = QD_AMQP_BAD_REQUEST;
    else {
        if (identity)
            al = qdr_auto_link_config_find_by_identity_CT(core, identity);
        else if (name)
            al = qdr_auto_link_config_find_by_name_CT(core, name);

        if (al) {
            qdr_route_del_auto_link_CT(core, al);
            query->status = QD_AMQP_NO_CONTENT;
        } else
            query->status = QD_AMQP_NOT_FOUND;
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}

void qdra_config_auto_link_create_CT(qdr_core_t          *core,
                                      qd_field_iterator_t *name,
                                      qdr_query_t         *query,
                                      qd_parsed_field_t   *in_body)
{
    while (true) {
        //
        // Ensure there isn't a duplicate name and that the body is a map
        //
        qdr_auto_link_t *al = DEQ_HEAD(core->auto_links);
        while (al) {
            if (name && al->name && qd_field_iterator_equal(name, (const unsigned char*) al->name))
                break;
            al = DEQ_NEXT(al);
        }

        if (!!al) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Name conflicts with an existing entity";
            break;
        }

        if (!qd_parse_is_map(in_body)) {
            query->status = QD_AMQP_BAD_REQUEST;
            break;
        }

        //
        // Extract the fields from the request
        //
        qd_parsed_field_t *addr_field       = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_ADDR]);
        qd_parsed_field_t *dir_field        = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_DIR]);
        qd_parsed_field_t *phase_field      = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_PHASE]);
        qd_parsed_field_t *connection_field = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_CONNECTION]);
        qd_parsed_field_t *container_field  = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_CONTAINER_ID]);

        //
        // Addr and dir fields are mandatory.  Fail if they're not both here.
        //
        if (!addr_field || !dir_field) {
            query->status = QD_AMQP_BAD_REQUEST;
            break;
        }

        qd_direction_t dir;
        const char *error = qdra_auto_link_direction_CT(dir_field, &dir);
        if (error) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = error;
            break;
        }

        //
        // Use the specified phase if present.  Otherwise default based on the direction:
        // Phase 0 for outgoing links and phase 1 for incoming links.
        //
        int phase = phase_field ? qd_parse_as_int(phase_field) : (dir == QD_OUTGOING ? 0 : 1);

        //
        // Validate the phase
        //
        if (phase < 0 || phase > 9) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "autoLink phase must be between 0 and 9";
            break;
        }

        //
        // The request is good.  Create the entity.
        //
        bool               is_container = !!container_field;
        qd_parsed_field_t *in_use_conn  = is_container ? container_field : connection_field;

        qdr_route_add_auto_link_CT(core, name, addr_field, dir, phase, in_use_conn, is_container);

        //
        // Compose the result map for the response.
        //
        if (query->body) {
            qd_compose_start_map(query->body);
            for (int col = 0; col < QDR_CONFIG_AUTO_LINK_COLUMN_COUNT; col++)
                qdr_config_auto_link_insert_column_CT(al, col, query->body, true);
            qd_compose_end_map(query->body);
        }

        query->status = QD_AMQP_CREATED;
        break;
    }

    //
    // Enqueue the response if there is a body. If there is no body, this is a management
    // operation created internally by the configuration file parser.
    //
    if (query->body) {
        //
        // If there was an error in processing the create, insert a NULL value into the body.
        //
        if (query->status.status / 100 > 2)
            qd_compose_insert_null(query->body);
        qdr_agent_enqueue_response_CT(core, query);
    } else {
        if (query->status.status / 100 > 2)
            qd_log(core->log, QD_LOG_ERROR, "Error configuring linkRoute: %s", query->status.description);
        free_qdr_query_t(query);
    }
}