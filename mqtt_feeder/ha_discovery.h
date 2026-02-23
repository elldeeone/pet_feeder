#ifndef HA_DISCOVERY_H
#define HA_DISCOVERY_H

#include "mqtt-c/mqtt.h"

/* Publish HA MQTT Discovery config for all entities. Returns 0 on success. */
int ha_discovery_publish(struct mqtt_client *client);

#endif
