/* Exercise the real message callback with a mock feeder. No serial or network
 * connection is opened and no food is dispensed. */
#include <assert.h>
#define main feeder_program_main
#define mqtt_publish test_publish
#include "../main.c"
#undef main
#undef mqtt_publish

static int feed_calls;
static int accepted_portions;
static char status_value[16];
static char portions_value[16];

enum MQTTErrors test_publish(struct mqtt_client *client, const char *topic,
                            const void *data, size_t size, uint8_t flags) {
    (void)client; (void)flags;
    char *target = strcmp(topic, TOPIC_STATUS) == 0 ? status_value : portions_value;
    assert(size < 16); memcpy(target, data, size); target[size] = '\0';
    return MQTT_OK;
}
int feeder_open(struct feeder *f, const char *path) { (void)f; (void)path; return 0; }
void feeder_close(struct feeder *f) { (void)f; }
int feeder_tick(struct feeder *f) { (void)f; return 0; }
int feeder_is_busy(struct feeder *f) { return f->state != FEEDER_IDLE; }
int feeder_feed(struct feeder *f, int portions) {
    if (feeder_is_busy(f)) return -1;
    ++feed_calls; accepted_portions = portions; f->state = FEEDER_FEEDING; return 0;
}
static void command(const char *topic, const void *payload, size_t size,
                    int retained, int duplicate) {
    struct mqtt_response_publish p = {0};
    p.topic_name = topic; p.topic_name_size = strlen(topic);
    p.application_message = payload; p.application_message_size = size;
    p.retain_flag = retained; p.dup_flag = duplicate;
    message_callback(NULL, &p);
}
int main(void) {
    command(TOPIC_PORTIONS_SET, "3.0", 3, 0, 0);
    assert(stored_portions == 3 && strcmp(portions_value, "3") == 0);
    const char *invalid[] = {"", "0", "11", "3junk", "3.5", "3.", "-1", "99999999999999999999999"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i)
        command(TOPIC_PORTIONS_SET, invalid[i], strlen(invalid[i]), 0, 0);
    command(TOPIC_PORTIONS_SET, "4\0junk", 6, 0, 0);
    assert(stored_portions == 3 && feed_calls == 0);
    command(TOPIC_FEED_SET, "PRESS", 5, 1, 0);
    command(TOPIC_FEED_SET, "PRESS", 5, 0, 1);
    command(TOPIC_FEED_SET, "", 0, 0, 0);
    command(TOPIC_FEED_SET, "press", 5, 0, 0);
    command(TOPIC_FEED_SET, "PRESSjunk", 9, 0, 0);
    assert(feed_calls == 0);
    command(TOPIC_FEED_SET, "PRESS", 5, 0, 0);
    assert(feed_calls == 1 && accepted_portions == 3);
    command(TOPIC_FEED_SET, "PRESS", 5, 0, 0);
    assert(feed_calls == 1); /* Busy guard preserves one dispense request. */
    publish_current_state();
    assert(strcmp(status_value, "feeding") == 0 && strcmp(portions_value, "3") == 0);
    feed.state = FEEDER_IDLE; publish_current_state();
    assert(strcmp(status_value, "idle") == 0 && strcmp(portions_value, "3") == 0);
    puts("PASS: command validation, busy guard and reconnect state; no actuator access");
    return 0;
}
