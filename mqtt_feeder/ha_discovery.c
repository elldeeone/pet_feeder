#include "ha_discovery.h"
#include <stdio.h>
#include <string.h>

#define TOPIC_PREFIX "pet_feeder"
#define AVAIL_TOPIC TOPIC_PREFIX "/available"

/* Shared device JSON fragment for all entities */
#define DEVICE_JSON \
    "\"dev\":{" \
        "\"ids\":[\"pet_feeder_advwin\"]," \
        "\"name\":\"Pet Feeder\"," \
        "\"mf\":\"Advwin\"," \
        "\"mdl\":\"6L Cat Feeder\"," \
        "\"sw\":\"mqtt_feeder 1.0\"" \
    "}"

#define AVAILABILITY_JSON \
    "\"avty_t\":\"" AVAIL_TOPIC "\"," \
    "\"pl_avail\":\"online\"," \
    "\"pl_not_avail\":\"offline\""

static int publish_retained(struct mqtt_client *client, const char *topic,
                            const char *payload) {
    enum MQTTErrors err;
    err = mqtt_publish(client, topic, payload, strlen(payload),
                       MQTT_PUBLISH_QOS_0 | MQTT_PUBLISH_RETAIN);
    if (err != MQTT_OK) {
        printf("[discovery] failed to publish %s: %s\n",
               topic, mqtt_error_str(err));
        return -1;
    }
    return 0;
}

int ha_discovery_publish(struct mqtt_client *client) {
    int rc = 0;

    printf("[discovery] publishing HA MQTT Discovery configs\n");

    /* Button: Feed (1 portion) */
    rc |= publish_retained(client,
        "homeassistant/button/pet_feeder/feed/config",
        "{"
            "\"name\":\"Feed\","
            "\"uniq_id\":\"pet_feeder_feed\","
            "\"cmd_t\":\"" TOPIC_PREFIX "/feed/set\","
            "\"ic\":\"mdi:food-drumstick\","
            AVAILABILITY_JSON ","
            DEVICE_JSON
        "}"
    );

    /* Number: Portions (1-10 slider) */
    rc |= publish_retained(client,
        "homeassistant/number/pet_feeder/portions/config",
        "{"
            "\"name\":\"Portions\","
            "\"uniq_id\":\"pet_feeder_portions\","
            "\"cmd_t\":\"" TOPIC_PREFIX "/portions/set\","
            "\"stat_t\":\"" TOPIC_PREFIX "/portions/state\","
            "\"min\":1,"
            "\"max\":10,"
            "\"step\":1,"
            "\"ic\":\"mdi:numeric\","
            AVAILABILITY_JSON ","
            DEVICE_JSON
        "}"
    );

    /* Binary sensor: Feeding status */
    rc |= publish_retained(client,
        "homeassistant/binary_sensor/pet_feeder/status/config",
        "{"
            "\"name\":\"Feeding\","
            "\"uniq_id\":\"pet_feeder_status\","
            "\"stat_t\":\"" TOPIC_PREFIX "/status\","
            "\"pl_on\":\"feeding\","
            "\"pl_off\":\"idle\","
            "\"dev_cla\":\"running\","
            "\"ic\":\"mdi:cat\","
            AVAILABILITY_JSON ","
            DEVICE_JSON
        "}"
    );

    if (rc == 0)
        printf("[discovery] all configs published\n");

    return rc;
}
