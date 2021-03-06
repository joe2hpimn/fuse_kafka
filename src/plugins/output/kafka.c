#ifndef TEST
#include <librdkafka/rdkafka.h>
#include <output_plugin.h>
#endif
#include "zookeeper.c"
char errstr[512];
/**
 * @brief a librdkafka callback called when a message is delivered
 **/
static void msg_delivered (rd_kafka_t *rk,
                         void *payload, size_t len,
                         int error_code,
                         void *opaque, void *msg_opaque) {

    /*trace_debug("msg_delivered: %s\n",
            (char*) payload);*/
}
/**
 * @brief a librdkafka callback called to log stuff from librdkafka
 **/
static void logger (const rd_kafka_t *rk, int level,
                 const char *fac, const char *buf) {
        /*struct timeval tv;
        gettimeofday(&tv, NULL);
        fprintf(stderr, "%u.%03u UGUU RDKAFKA-%i-%s: %s: %s\n",
                (int)tv.tv_sec, (int)(tv.tv_usec / 1000),
                level, fac, rd_kafka_name(rk), buf);*/
        /*trace_debug("logger: RDKAFKA-%i-%s: %s: %s\n",
                level, fac, rk == (void*) 0 ? 0 : rd_kafka_name(rk), buf);*/
}
/**
 * @brief setup_kafka initialises librdkafka based on the config
 * wrapped in kafka_t
 * @param k kafka configuration
 **/
int output_setup(kafka_t* k, config* fk_conf)
{
    //trace_debug("kafka output_setup: entry");
    if(fk_conf == NULL) return 1;
    char* brokers = NULL;
    char* zookeepers = NULL;
    char* topic = "logs";
    //trace_debug("kafka output_setup: fk_conf %x", fk_conf);
    if(fk_conf->zookeepers_n > 0) zookeepers = fk_conf->zookeepers[0];
    if(fk_conf->brokers_n > 0) brokers = fk_conf->brokers[0];
    if(fk_conf->topic_n > 0) topic = fk_conf->topic[0];
    rd_kafka_topic_conf_t *topic_conf;
    rd_kafka_conf_t *conf;
    conf = rd_kafka_conf_new();
    rd_kafka_conf_set_dr_cb(conf, msg_delivered);
    if(rd_kafka_conf_set(conf, "debug", "all",
                errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK || 
            rd_kafka_conf_set(conf, "batch.num.messages", "1",
                errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        printf("%% Debug configuration failed: %s: %s\n",
                errstr, "all");
        return(1);
    }
    if (!(k->rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf,
                    errstr, sizeof(errstr)))) {
        fprintf(stderr,
                "%% Failed to create new producer: %s\n",
                errstr);
        return(1);
    }
    //trace_debug("kafka output_setup: before set_logger");
    rd_kafka_set_logger(k->rk, logger);
    rd_kafka_set_log_level(k->rk, 7);
    //trace_debug("kafka output_setup: zookeepers: %s, brokers %s", zookeepers, brokers);
    if (zookeepers != NULL)
    {
        k->zhandle = initialize_zookeeper(zookeepers, k);
        //trace_debug("output_setup: initialize_zookeeper done");
        return 0;
    }
    else if(brokers != NULL)
    {
        if (rd_kafka_brokers_add(k->rk, brokers) == 0) {
            fprintf(stderr, "%% No valid brokers specified\n");
            return(1);
        }
        topic_conf = rd_kafka_topic_conf_new();
        k->rkt = rd_kafka_topic_new(k->rk, topic, topic_conf);
        if(k->rkt == NULL)
            printf("topic %s creation failed\n", topic);
        return k->rkt == NULL;
    }
    return 0;
}
int output_update(kafka_t* k)
{
    if(k != NULL && k->conf != NULL && k->conf->zookeepers_n > 0)
        k->zhandle = initialize_zookeeper(k->conf->zookeepers[0], k);
}
/**
 * @brief send a string to kafka
 * @param k configuration with kafka
 * @param buf string to serialize
 * @param len size of the string to save
 **/
int output_send(kafka_t* k, char* buf, size_t len)
{
    int r = 0;
    static int i = 0;
    //trace_debug("output_send: k = %x k->rkt = %x", k, k == 0 ? 0: k->rkt);
    if(k != 0 && k->rkt > (void*) 1 && (r = rd_kafka_produce(k->rkt, RD_KAFKA_PARTITION_UA,
            RD_KAFKA_MSG_F_COPY,
            buf, len,
            NULL, 0, NULL)))
    {
        /*trace_debug("rd_kafka_produce failed");
        if(i++ % 100 == 0) trace_error("output_send: "
                "rd_kafka_produce: failed %d times (last rc %d) ", i, r);*/
    }
    else i = 0;
    /*trace_debug("%% Sent %zd bytes to topic "
            "%s\n", len, k ==  0 || k->rkt <= (void*) 2 ?
            0 : rd_kafka_topic_name(k->rkt));*/
    if(k != 0 && k->rk != 0) rd_kafka_poll(k->rk, 0);
    /*if((r = rd_kafka_poll(k->rk, 10)) != 1)
        printf("============= rd_kafka_poll: failed %d\n", r);*/
    /*while(rd_kafka_poll(k->rk, 1000) != -1)
        continue;*/
    return 0;
}
int output_clean(kafka_t* k)
{
    if(k == NULL) return;
    if(k->rkt > (void*) 1) rd_kafka_topic_destroy(k->rkt);
    rd_kafka_destroy(k->rk);
    rd_kafka_wait_destroyed(1000);
    return 1;
}
