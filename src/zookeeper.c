/*
 * librdkafka - Apache Kafka C library
 * *
 * Copyright (c) 2012, Magnus Edenhill
 * All rights reserved.
 * *
 * Redistribution and use in source and binary forms, with or
 * without
 * modification, are permitted provided that the following
 * conditions are met:
 * *
 * 1. Redistributions of source code must retain the above copyright
 * notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 * copyright notice,
 * this list of conditions and the following disclaimer in the
 * documentation
 * and/or other materials provided with the distribution.
 * *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef FUSE_KAFKA_ZOOKEEPER_C
#define FUSE_KAFKA_ZOOKEEPER_C
#include <jansson.h>
#define BROKER_PATH "/brokers/ids"
#include "server_list.c"
#include "trace.h"
static void set_brokerlist_from_zookeeper(zhandle_t *zzh, char *brokers)
{
    if (zzh)
    {
        struct String_vector brokerlist;
        if (zoo_get_children(zzh, BROKER_PATH, 1, &brokerlist) != ZOK)
        {
            fprintf(stderr, "No brokers found on path %s\n", BROKER_PATH);
            return;
        }
        int i;
        char *brokerptr = brokers;
        for (i = 0; i < brokerlist.count; i++)
        {
            char path[255], cfg[1024];
            sprintf(path, "/brokers/ids/%s", brokerlist.data[i]);
            int len = sizeof(cfg);
            zoo_get(zzh, path, 0, cfg, &len, NULL);
            if (len > 0)
            {
                cfg[len] = '\0';
                json_error_t jerror;
                json_t *jobj = json_loads(cfg, 0, &jerror);
                if (jobj)
                {
                    json_t *jhost = json_object_get(jobj, "host");
                    json_t *jport = json_object_get(jobj, "port");
                    if (jhost && jport)
                    {
                        const char *host = json_string_value(jhost);
                        const int port = json_integer_value(jport);
                        sprintf(brokerptr, "%s:%d", host, port);
                        brokerptr += strlen(brokerptr);
                        if (i < brokerlist.count - 1)
                        {
                            *brokerptr++ = ',';
                        }
                    }
                    json_decref(jobj);
                }
            }
        }
        deallocate_String_vector(&brokerlist);
    }
}
static void watcher(zhandle_t *zh, int type,
        int state, const char *path, void *param)
{
    char brokers[1024];
    kafka_t* k = (kafka_t*) param;
    rd_kafka_topic_conf_t *topic_conf;
    if(k->conf == NULL) return;
    char* topic;
    if(k->conf->topic_n <= 0)
    {
        trace("topic missing");
        return;
    }
    topic = k->conf->topic[0];
    if (k->no_brokers || type == ZOO_CHILD_EVENT && strncmp(
                path, BROKER_PATH, sizeof(BROKER_PATH) - 1) == 0)
    {
        brokers[0] = '\0';
        set_brokerlist_from_zookeeper(zh, brokers);
        if (brokers[0] != '\0' && k->rk != NULL &&
                server_list_add_once(&(k->broker_list), brokers))
        {
            rd_kafka_brokers_add(k->rk, brokers);
            k->no_brokers = 0;
            rd_kafka_poll(k->rk, 10);
            topic_conf = rd_kafka_topic_conf_new();
            k->rkt = rd_kafka_topic_new(k->rk, topic, topic_conf);
            if(k->rkt == NULL)
                printf("topic %s creation failed\n", topic);
        }
    }
}
static zhandle_t* initialize_zookeeper(const char * zookeeper, void* param)
{
    zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);
    ((kafka_t*) param)->no_brokers = 1;
    ((kafka_t*) param)->broker_list = NULL;
    zhandle_t *zh;
    zh = zookeeper_init(zookeeper, watcher, 10000, 0, param, 0);
    if (zh == NULL)
    {
        fprintf(stderr, "Zookeeper connection not established.");
        //exit(1);
    }
    return zh;
}
#endif
