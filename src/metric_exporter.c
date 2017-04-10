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

#include "metric_exporter_private.h"
#include "dispatch_private.h"
#include "metric_private.h"
#include <stdio.h>

#define MIN(a, b) (a) < (b) ? (a) : (b)

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

typedef enum {
    METRIC_TYPE_GAUGE = 1,
    METRIC_TYPE_COUNTER
} metric_type_t;

static const char *
type_to_string(metric_type_t type)
{
    switch (type) {
    case METRIC_TYPE_GAUGE:
        return "gauge";
    case METRIC_TYPE_COUNTER:
        return "counter";
    default:
        return "unknown";
    }
}

static void
qd_metric_write(qd_metric_t *metric, qd_buffer_list_t *buffers)
{
    qd_metric_value_t * value = DEQ_HEAD(metric->values);

    char buf[256];
    snprintf(buf, sizeof(buf), "# HELP %s %s\n", metric->name, metric->description);
    write_string(buffers, buf, strlen(buf));
    snprintf(buf, sizeof(buf), "# TYPE %s %s\n", metric->name, type_to_string(metric->type));
    write_string(buffers, buf, strlen(buf));

    while (value != NULL) {
        write_string(buffers, metric->name, strlen(metric->name));
        if (value->num_labels >= 1 && value->labels[0].key != NULL) {
            write_string(buffers, "{", 1);
            for (int i = 0; i < value->num_labels; i++) {
                write_string(buffers, value->labels[i].key, strlen(value->labels[i].key));
                write_string(buffers, "=\"", 2);
                write_string(buffers, value->labels[i].value, strlen(value->labels[i].value));
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

typedef void (*metric_collector_t)(qd_metric_list_t *metrics, pn_data_t *body);

struct metric_query_ctx_t {
    qdr_query_t *query;
    qd_composed_field_t *field;
    metric_callback_t callback;
    metric_collector_t collector;

    int count;
    int current_count;
    void *callback_ctx;

    qd_metric_list_t metric_list;
};

typedef struct metric_query_ctx_t metric_query_ctx_t;

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

void
metric_query_response_handler(void *context, const qd_amqp_error_t *status, bool more)
{
    printf("IN QUERY CALLBACK\n");
    metric_query_ctx_t *ctx = (metric_query_ctx_t *)context;

    if (status->status / 100 == 2) {
        if (more) {
            ctx->current_count++;
            if (ctx->count != ctx->current_count) {
                qdr_query_get_next(ctx->query);
                return;
            } else {
                qdr_query_free(ctx->query);
            }
        }
    }

    qd_compose_end_list(ctx->field);
    qd_compose_end_map(ctx->field);

    qd_composed_field_t * field = ctx->field;

    qd_buffer_list_t content;
    qd_compose_take_buffers(field, &content);

    unsigned int length = qd_buffer_list_length(&content);
    char * buf = malloc(length);
    flatten_bufs(buf, &content);
    pn_data_t *body = pn_data(512);
    ssize_t written = pn_data_decode(body, buf, length);
    free(buf);
    printf("Decoded data: %ld bytes out of %u\n", written, length);

    pn_type_t type = pn_data_type(body);
    printf("Got data with type: %s\n", pn_type_name(type));

    size_t count = pn_data_get_map(body);
    printf("Found map with %lu entries\n", count);
	pn_data_enter(body);
	for (size_t i = 0; i < count/2; i++) {
		// read key
        pn_bytes_t key;
	  	if (pn_data_next(body)) {
			switch (pn_data_type(body)) {
		  		case PN_STRING:
                    key = pn_data_get_string(body);
					break;
				default:
					break;
			}
		}

        // get value;
        pn_data_next(body);
        if (strncmp(key.start, "results", key.size) == 0) {
            ctx->collector(&ctx->metric_list, body);
        }
	}
	pn_data_exit(body);

    qd_buffer_list_t callback_data;
    DEQ_INIT(callback_data);

    qd_metric_t *metric = DEQ_HEAD(ctx->metric_list);
    while (metric != NULL) {
        qd_metric_write(metric, &callback_data);
        metric = DEQ_NEXT(metric);
    }

    ctx->callback(callback_data, ctx->callback_ctx);

    pn_data_free(body);

    printf("IN QUERY CALLBACK, DONE\n");
	qd_compose_free(field);
	free(ctx);
}

static void
metric_connection_collector(qd_metric_list_t *metrics, pn_data_t *body)
{
    size_t num_connections = pn_data_get_list(body);
    qd_metric_t *metric = qd_metric("connections", "Number of connections", METRIC_TYPE_GAUGE);

    pn_data_enter(body);
    for (size_t i = 0; i < num_connections; i++) {
        if (pn_data_next(body)) {
            size_t entries = pn_data_get_map(body);
            pn_data_enter(body);
            for (size_t j = 0; j < entries/2; j++) {
                pn_bytes_t key;
                if (pn_data_next(body)) {
                    if (pn_data_type(body) == PN_STRING) {
                        key = pn_data_get_string(body);
                    }
                }

                pn_data_next(body);
                if (strncmp(key.start, "container", key.size) == 0) {
                    pn_bytes_t value = pn_data_get_string(body);
                    QD_METRIC_INC_L1(metric, "container", value.start);
                }
            }
            pn_data_exit(body);
        }
    }
    pn_data_exit(body);

    DEQ_INSERT_TAIL(*metrics, metric);
}

void
metric_export_prometheus(qd_dispatch_t *dispatch, metric_callback_t callback, void *callback_ctx)
{

    metric_query_ctx_t * ctx = malloc(sizeof(metric_query_ctx_t));
    if (!ctx) {
        printf("BUHUOOOO\n");
        return;
    }

    ctx->callback = callback;
    ctx->callback_ctx = callback_ctx;
    ctx->count = -1;
    ctx->current_count = 0;
    ctx->field = qd_compose_subfield(0);
    ctx->collector = metric_connection_collector;
    DEQ_INIT(ctx->metric_list);

    qd_compose_start_map(ctx->field);

    qd_compose_insert_string(ctx->field, "attributeNames");

    qd_parsed_field_t *attribute_names_parsed_field = NULL;
    printf("Created query\n");

    ctx->query = qdr_manage_query(dispatch->router->router_core, ctx, QD_ROUTER_CONNECTION, attribute_names_parsed_field, ctx->field, metric_query_response_handler);

    qdr_query_add_attribute_names(ctx->query);
    qd_compose_insert_string(ctx->field, "results");
    qd_compose_start_list(ctx->field);

    printf("Sending off query\n");
    qdr_query_get_first(ctx->query, 0);
}
