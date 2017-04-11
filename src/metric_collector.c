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

#include "metric_collector.h"
#include "dispatch_private.h"
#include "metric_private.h"
#include <stdio.h>

typedef void (*metric_result_parser_t)(qd_metric_list_t *metrics, pn_data_t *body, pn_handle_t attributes, pn_handle_t results);
typedef struct metric_collector_t metric_collector_t;
typedef struct metric_collect_context_t metric_collect_context_t;

struct metric_collector_t {
    DEQ_LINKS(metric_collector_t);
    qdr_query_t *query;
    int count;
    int current_count;
    qd_composed_field_t *field;
    pn_data_t *response;

    metric_result_parser_t parser;
    metric_collect_context_t *parent_ctx;
};

DEQ_DECLARE(metric_collector_t, metric_collector_list_t);

struct metric_collect_context_t {
    metric_callback_t callback;
    void *callback_ctx;

    qd_metric_list_t metric_list;
    metric_collector_list_t collector_list;
    int completed;
};

typedef struct metric_collect_context_t metric_collect_context_t;

#define MIN(a, b) (a) < (b) ? (a) : (b)
static void write_string(qd_buffer_list_t *buffers, const char *str, unsigned long long len);
static void metric_collect_start(metric_collect_context_t *ctx);
static void metric_collect_add_collector(qd_dispatch_t *dispatch, metric_collect_context_t *parent_ctx, qd_router_entity_type_t entity_type, metric_result_parser_t parser);
static void metric_connection_collector(qd_metric_list_t *metrics, pn_data_t *body, pn_handle_t attributes, pn_handle_t results);
//static qd_metric_label_t map_get_label(pn_data_t *body, const char *key);
static void map_get_entry(pn_data_t *body, const char *key, pn_handle_t *key_handle, pn_handle_t *value_handle);
static void metric_query_response_handler(void *context, const qd_amqp_error_t *status, bool more);
static pn_data_t * metric_decode_query_response(qd_composed_field_t *field);
static void qd_metric_write(qd_metric_t *metric, qd_buffer_list_t *buffers);
static size_t flatten_bufs(char * buffer, qd_buffer_list_t *content);

void
metric_collect(qd_dispatch_t *dispatch, metric_callback_t callback, void *callback_ctx)
{

    metric_collect_context_t * ctx = malloc(sizeof(metric_collect_context_t));
    if (!ctx) {
        return;
    }

    ctx->completed = 0;
    ctx->callback = callback;
    ctx->callback_ctx = callback_ctx;
    DEQ_INIT(ctx->metric_list);
    DEQ_INIT(ctx->collector_list);

    printf("Adding collector\n");
    metric_collect_add_collector(dispatch, ctx, QD_ROUTER_CONNECTION, metric_connection_collector);
    printf("Starting collector\n");
    metric_collect_start(ctx);
}

void
metric_query_response_handler(void *context, const qd_amqp_error_t *status, bool more)
{
    printf("IN QUERY CALLBACK\n");
    metric_collector_t *collector = (metric_collector_t *)context;

    if (status->status / 100 == 2) {
        if (more) {
            collector->current_count++;
            if (collector->count != collector->current_count) {
                qdr_query_get_next(collector->query);
                return;
            } else {
                qdr_query_free(collector->query);
            }
        }
    }
    qd_compose_end_list(collector->field);
    qd_compose_end_map(collector->field);


    pn_data_t *body = metric_decode_query_response(collector->field);
    collector->response = body;
    metric_collect_context_t *ctx = collector->parent_ctx;

    pn_handle_t attribute_names_key, attribute_names_value;
    pn_handle_t results_key, results_value;

    map_get_entry(body, "results", &results_key, &results_value);
    map_get_entry(body, "attributeNames", &attribute_names_key, &attribute_names_value);

    collector->parser(&ctx->metric_list, body, attribute_names_value, results_value);

    if (++ctx->completed == DEQ_SIZE(ctx->collector_list)) {
        qd_buffer_list_t buffers;
        DEQ_INIT(buffers);


        qd_metric_t *metric = DEQ_HEAD(ctx->metric_list);
        while (metric != NULL) {
            qd_metric_write(metric, &buffers);
            DEQ_REMOVE_HEAD(ctx->metric_list);
            qd_metric_free(metric);
            metric = DEQ_HEAD(ctx->metric_list);
        }

        ctx->callback(buffers, ctx->callback_ctx);

        collector = DEQ_HEAD(ctx->collector_list);
        while (collector != NULL) {
            DEQ_REMOVE_HEAD(ctx->collector_list);
            pn_data_free(collector->response);
            qd_compose_free(collector->field);
            free(collector);
            collector = DEQ_HEAD(ctx->collector_list);
        }
        free(ctx);
    }
}

static size_t flatten_bufs(char * buffer, qd_buffer_list_t *content)
{
    char        *cursor = buffer;
    qd_buffer_t *buf    = DEQ_HEAD(*content);

    while (buf) {
        memcpy(cursor, qd_buffer_base(buf), qd_buffer_size(buf));
        cursor += qd_buffer_size(buf);
        buf = buf->next;
    }

    return (size_t) (cursor - buffer);
}

pn_data_t *
metric_decode_query_response(qd_composed_field_t *field)
{

    qd_buffer_list_t content;
    qd_compose_take_buffers(field, &content);

    unsigned int length = qd_buffer_list_length(&content);
    char *buf = malloc(length);
    if (!buf) {
        return NULL;
    }

    flatten_bufs(buf, &content);
    pn_data_t *body = pn_data(0);
    ssize_t written = pn_data_decode(body, buf, length);
    free(buf);
    printf("Decoded data: %ld bytes out of %u\n", written, length);
    return body;
}

#if 0
static qd_metric_label_t
map_get_label(pn_data_t *body, const char *key)
{
    qd_metric_label_t label;
    pn_handle_t key_handle;
    pn_handle_t value_handle;

    map_get_entry(body, key, &key_handle, &value_handle);

    pn_data_restore(body, key_handle);
    label.key = pn_data_get_string(body);

    pn_data_restore(body, value_handle);
    label.value = pn_data_get_string(body);
    return label;
}
#endif

static void
map_get_entry(pn_data_t *body, const char *key, pn_handle_t *key_handle, pn_handle_t *value_handle)
{
    size_t entries = pn_data_get_map(body);
    pn_data_enter(body);
    for (size_t j = 0; j < entries/2; j++) {
        pn_bytes_t entry_key = {0, 0};
        if (pn_data_next(body)) {
            if (pn_data_type(body) == PN_STRING) {
                entry_key = pn_data_get_string(body);
                *key_handle = pn_data_point(body);
            }
        }

        if (pn_data_next(body)) {
            if (strncmp(entry_key.start, key, strlen(key)) == 0) {
                *value_handle = pn_data_point(body);
                break;
            }
        }
    }
    pn_data_exit(body);
}

typedef struct keyset_t keyset_t;

struct keyset_t {
    const char *keystr;
    pn_bytes_t key;
    size_t keyidx;
};

static void
metric_connection_collector(qd_metric_list_t *metrics, pn_data_t *body, pn_handle_t attributes, pn_handle_t results)
{

    qd_metric_t *metric = qd_metric("connections", "Number of connections", QD_METRIC_TYPE_GAUGE);


    keyset_t keyset[3] = { { .keystr = "container" }, { .keystr = "dir" }, { .keystr = "role" } };

    pn_data_restore(body, attributes);
    size_t num_connections = pn_data_get_list(body);
    printf("Found data type %s for list with %lu entries\n", pn_type_name(pn_data_type(body)), num_connections);
    pn_data_enter(body);
    for (size_t i = 0; i < num_connections; i++) {
        if (pn_data_next(body)) {
            size_t keys = pn_data_get_list(body);
            printf("Keys %lu\n", keys);
            pn_data_enter(body);
            for (size_t k = 0; k < keys; k++) {
                if (pn_data_next(body)) {
                    printf("Type %s\n", pn_type_name(pn_data_type(body)));
                    if (pn_data_type(body) == PN_STRING) {
                        pn_bytes_t str = pn_data_get_string(body);
                        for (int s = 0; s < sizeof(keyset); s++) {
                            if (strncmp(str.start, keyset[s].keystr, strlen(keyset[s].keystr)) == 0) {
                                keyset[s].keyidx = k;
                                keyset[s].key = str;
                            }
                        }
                    }
                }
            }
            pn_data_exit(body);
        }
    }

    pn_data_restore(body, results);
    pn_data_enter(body);
    for (size_t i = 0; i < num_connections; i++) {
        if (pn_data_next(body)) {
            qd_metric_label_t labels[3];

            size_t keys = pn_data_get_list(body);
            printf("Keys %lu\n", keys);
            pn_data_enter(body);
            for (size_t k = 0; k < keys; k++) {
                if (pn_data_next(body)) {
                    for (int s = 0; s < sizeof(keyset); s++) {
                        if (keyset[s].keyidx == k) {
                            pn_bytes_t str = pn_data_get_string(body);
                            labels[s].key = keyset[s].key;
                            labels[s].value = str;
                        }
                    }
                }
            }
            qd_metric_inc(metric, 1, labels, 3);
            pn_data_exit(body);
        }
    }
    pn_data_exit(body);
    DEQ_INSERT_TAIL(*metrics, metric);
}

static void
metric_collect_add_collector(qd_dispatch_t *dispatch, metric_collect_context_t *parent_ctx, qd_router_entity_type_t entity_type, metric_result_parser_t parser)
{
    metric_collector_t * ctx = malloc(sizeof(metric_collector_t));
    if (!ctx) {
        return;
    }
    ctx->count = -1;
    ctx->current_count = 0;
    ctx->field = qd_compose_subfield(0);
    ctx->parser = parser;
    ctx->parent_ctx = parent_ctx;
    DEQ_ITEM_INIT(ctx);

    qd_compose_start_map(ctx->field);

    qd_compose_insert_string(ctx->field, "attributeNames");

    qd_parsed_field_t *attribute_names_parsed_field = NULL;
    printf("Created query\n");

    ctx->query = qdr_manage_query(dispatch->router->router_core, ctx, entity_type, attribute_names_parsed_field, ctx->field, metric_query_response_handler);

    qdr_query_add_attribute_names(ctx->query);
    qd_compose_insert_string(ctx->field, "results");
    qd_compose_start_list(ctx->field);
    DEQ_INSERT_TAIL(parent_ctx->collector_list, ctx);
}

static void
metric_collect_start(metric_collect_context_t *ctx)
{
    metric_collector_t *collector = DEQ_HEAD(ctx->collector_list);
    while (collector != NULL) {
        qdr_query_get_first(collector->query, 0);
        collector = DEQ_NEXT(collector);
    }
}

/**********************************************
 * Functions for formatting prometheus output *
 **********************************************/
static void
write_string(qd_buffer_list_t *buffers, const char *str, unsigned long long len)
{
    qd_buffer_t * buf = DEQ_TAIL(*buffers);
    while (len > 0) {
        if (buf == NULL) {
            buf = qd_buffer();
            DEQ_INSERT_TAIL(*buffers, buf);
        }
        unsigned char * p = qd_buffer_cursor(buf);
        unsigned long long to_copy = MIN(len, qd_buffer_capacity(buf));
        memcpy(p, str, to_copy);
        qd_buffer_insert(buf, to_copy);
        str += to_copy;
        len -= to_copy;
        if (len > 0) {
            buf = qd_buffer();
            DEQ_INSERT_TAIL(*buffers, buf);
        }
    }
}

static void
qd_metric_write(qd_metric_t *metric, qd_buffer_list_t *buffers)
{
    qd_metric_value_t * value = DEQ_HEAD(metric->values);

    char buf[256];
    snprintf(buf, sizeof(buf), "# HELP %s %s\n", metric->name, metric->description);
    write_string(buffers, buf, strlen(buf));
    snprintf(buf, sizeof(buf), "# TYPE %s %s\n", metric->name, qd_metric_type_string(metric->type));
    write_string(buffers, buf, strlen(buf));

    while (value != NULL) {
        write_string(buffers, metric->name, strlen(metric->name));
        if (value->num_labels >= 1 && value->labels[0].key.size > 0 ) {
            write_string(buffers, "{", 1);
            for (int i = 0; i < value->num_labels; i++) {
                write_string(buffers, value->labels[i].key.start, value->labels[i].key.size);
                write_string(buffers, "=\"", 2);
                write_string(buffers, value->labels[i].value.start, value->labels[i].value.size);
                write_string(buffers, "\"", 1);
                if (i < value->num_labels - 1) {
                    write_string(buffers, ",", 1);
                }
            }
            write_string(buffers, "}", 1);
        }
        write_string(buffers, " ", 1);

        snprintf(buf, sizeof(buf), "%f\n", value->value);
        write_string(buffers, buf, strlen(buf));
        value = DEQ_NEXT(value);
    }
}
