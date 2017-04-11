#ifndef __dispatch_metric_h__
#define __dispatch_metric_h__ 1
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

#include <proton/codec.h>

typedef struct qd_metric_t         qd_metric_t;
typedef struct qd_metric_label_t   qd_metric_label_t;

typedef enum {
    QD_METRIC_TYPE_GAUGE = 1,
    QD_METRIC_TYPE_COUNTER
} qd_metric_type_t;

struct qd_metric_label_t {
    pn_bytes_t key;
    pn_bytes_t value;
};

const char * qd_metric_type_string(qd_metric_type_t type);
qd_metric_t * qd_metric(const char *name, const char *description, qd_metric_type_t type);
void          qd_metric_free(qd_metric_t *metric);
void          qd_metric_inc(qd_metric_t *metric, double increment, const qd_metric_label_t labels[], unsigned int num_labels);
void          qd_metric_dec(qd_metric_t *metric, double decrement, const qd_metric_label_t labels[], unsigned int num_labels);
void          qd_metric_set(qd_metric_t *metric, double value, const qd_metric_label_t labels[], unsigned int num_labels);

#endif