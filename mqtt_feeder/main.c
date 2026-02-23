#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>

#include "mqtt-c/mqtt.h"
#include "config.h"
#include "feeder.h"
#include "ha_discovery.h"

#define TOPIC_PREFIX      "pet_feeder"
#define TOPIC_FEED_SET    TOPIC_PREFIX "/feed/set"
#define TOPIC_PORTIONS_SET TOPIC_PREFIX "/portions/set"
#define TOPIC_PORTIONS_STATE TOPIC_PREFIX "/portions/state"
#define TOPIC_STATUS      TOPIC_PREFIX "/status"
#define TOPIC_AVAIL       TOPIC_PREFIX "/available"

#define CLIENT_ID         "pet_feeder"
#define KEEP_ALIVE_SEC    60
#define RECONNECT_SEC     5
#define MAIN_LOOP_MS      100

/* MQTT buffers — sized for a 64MB device */
static uint8_t sendbuf[4096] __attribute__((aligned(4)));
static uint8_t recvbuf[2048];

static struct mqtt_client mqtt;
static struct config cfg;
static struct feeder feed;
static volatile sig_atomic_t running = 1;
static int last_busy = -1; /* track state changes */
static int stored_portions = 1; /* portions to dispense when Feed is pressed */

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

static int open_tcp_socket(const char *host, int port) {
    struct addrinfo hints, *res, *p;
    char port_str[16];
    int fd = -1, flags;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        printf("[mqtt] DNS lookup failed for %s\n", host);
        return -1;
    }

    for (p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        printf("[mqtt] failed to connect to %s:%d\n", host, port);
        return -1;
    }

    /* Set non-blocking for MQTT-C */
    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    printf("[mqtt] connected to %s:%d\n", host, port);
    return fd;
}

static void publish_status(int busy) {
    const char *status = busy ? "feeding" : "idle";
    mqtt_publish(&mqtt, TOPIC_STATUS, status, strlen(status),
                 MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN);
}

static void publish_portions_state(int value) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    mqtt_publish(&mqtt, TOPIC_PORTIONS_STATE, buf, strlen(buf),
                 MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN);
}

/* Called by MQTT-C when we receive a publish from the broker */
static void message_callback(void **state, struct mqtt_response_publish *pub) {
    char topic[256];
    char payload[64];
    size_t tlen, plen;
    (void)state;

    tlen = pub->topic_name_size;
    if (tlen >= sizeof(topic)) tlen = sizeof(topic) - 1;
    memcpy(topic, pub->topic_name, tlen);
    topic[tlen] = '\0';

    plen = pub->application_message_size;
    if (plen >= sizeof(payload)) plen = sizeof(payload) - 1;
    memcpy(payload, pub->application_message, plen);
    payload[plen] = '\0';

    printf("[mqtt] received: %s = %s\n", topic, payload);

    if (strcmp(topic, TOPIC_FEED_SET) == 0) {
        /* Feed button pressed — dispense stored_portions */
        if (feeder_feed(&feed, stored_portions) == 0) {
            publish_status(1);
            last_busy = 1;
        }
    } else if (strcmp(topic, TOPIC_PORTIONS_SET) == 0) {
        /* Slider changed — just store the value, don't feed */
        int n = atoi(payload);
        if (n >= 1 && n <= 10) {
            stored_portions = n;
            publish_portions_state(n);
            printf("[main] portions set to %d\n", n);
        }
    }
}

/* Called by MQTT-C on connect and reconnect */
static void reconnect_callback(struct mqtt_client *client, void **state) {
    int fd;
    (void)state;

    /* Log the error that triggered this reconnect */
    if (client->error != MQTT_ERROR_INITIAL_RECONNECT) {
        printf("[mqtt] reconnecting (error: %s)\n",
               mqtt_error_str(client->error));
        sleep(RECONNECT_SEC);
    }

    /* Close old socket if any */
    if (client->socketfd >= 0)
        close(client->socketfd);

    printf("[mqtt] connecting to %s:%d...\n", cfg.broker_host, cfg.broker_port);

    fd = open_tcp_socket(cfg.broker_host, cfg.broker_port);
    if (fd < 0) {
        printf("[mqtt] reconnect failed, will retry in %ds\n", RECONNECT_SEC);
        sleep(RECONNECT_SEC);
        return;
    }

    mqtt_reinit(client, fd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf));

    mqtt_connect(client, CLIENT_ID,
                 TOPIC_AVAIL, "offline", 7,  /* LWT */
                 cfg.mqtt_user[0] ? cfg.mqtt_user : NULL,
                 cfg.mqtt_pass[0] ? cfg.mqtt_pass : NULL,
                 MQTT_CONNECT_CLEAN_SESSION | MQTT_CONNECT_WILL_QOS_0 | MQTT_CONNECT_WILL_RETAIN,
                 KEEP_ALIVE_SEC);

    mqtt_subscribe(client, TOPIC_FEED_SET, 0);
    mqtt_subscribe(client, TOPIC_PORTIONS_SET, 0);

    /* Publish online status */
    mqtt_publish(client, TOPIC_AVAIL, "online", 6,
                 MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN);

    /* Publish initial state */
    publish_status(0);
    publish_portions_state(1);

    /* Publish HA Discovery */
    ha_discovery_publish(client);

    printf("[mqtt] session established\n");
    last_busy = 0;
}

int main(int argc, char *argv[]) {
    const char *config_path = CONFIG_PATH;
    struct timespec ts;

    /* Line-buffer stdout so logs flush immediately when redirected to file */
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("mqtt_feeder v1.1 — Pet Feeder MQTT Bridge\n");

    if (argc > 1)
        config_path = argv[1];

    /* Load config */
    config_load(&cfg, config_path);

    /* Open serial port */
    if (feeder_open(&feed, cfg.serial_path) < 0) {
        printf("[main] FATAL: cannot open serial port\n");
        return 1;
    }

    /* Set up signals */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize MQTT with reconnect support */
    mqtt_init_reconnect(&mqtt, reconnect_callback, NULL, message_callback);

    printf("[main] entering main loop (%dms interval)\n", MAIN_LOOP_MS);

    while (running) {
        /* Process MQTT I/O (triggers reconnect if in error state) */
        mqtt_sync(&mqtt);

        /* Tick the feeder state machine */
        feeder_tick(&feed);

        /* Publish status changes */
        int busy = feeder_is_busy(&feed);
        if (busy != last_busy) {
            publish_status(busy);
            last_busy = busy;
        }

        /* Sleep 100ms */
        ts.tv_sec = 0;
        ts.tv_nsec = MAIN_LOOP_MS * 1000000L;
        nanosleep(&ts, NULL);
    }

    printf("[main] shutting down\n");

    /* Publish offline before disconnect */
    mqtt_publish(&mqtt, TOPIC_AVAIL, "offline", 7,
                 MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN);
    mqtt_sync(&mqtt);
    mqtt_disconnect(&mqtt);

    if (mqtt.socketfd >= 0)
        close(mqtt.socketfd);

    feeder_close(&feed);

    printf("[main] bye\n");
    return 0;
}
