#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void trim(char *s) {
    char *end;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
}

static void set_defaults(struct config *cfg) {
    snprintf(cfg->broker_host, sizeof(cfg->broker_host), "%s", DEFAULT_BROKER_HOST);
    cfg->broker_port = DEFAULT_BROKER_PORT;
    snprintf(cfg->serial_path, sizeof(cfg->serial_path), "%s", DEFAULT_SERIAL_PATH);
    cfg->mqtt_user[0] = '\0';
    cfg->mqtt_pass[0] = '\0';
}

int config_load(struct config *cfg, const char *path) {
    FILE *f;
    char line[512];

    set_defaults(cfg);

    f = fopen(path, "r");
    if (!f) {
        printf("[config] %s not found, using defaults\n", path);
        return 0;
    }

    while (fgets(line, sizeof(line), f)) {
        char *eq, *key, *val;

        /* skip comments and blank lines */
        key = line;
        while (isspace((unsigned char)*key)) key++;
        if (*key == '#' || *key == '\0' || *key == '\n')
            continue;

        eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        key = line;
        val = eq + 1;
        trim(key);
        trim(val);

        if (strcmp(key, "broker_host") == 0) {
            snprintf(cfg->broker_host, sizeof(cfg->broker_host), "%s", val);
        } else if (strcmp(key, "broker_port") == 0) {
            cfg->broker_port = atoi(val);
        } else if (strcmp(key, "serial_path") == 0) {
            snprintf(cfg->serial_path, sizeof(cfg->serial_path), "%s", val);
        } else if (strcmp(key, "mqtt_user") == 0) {
            snprintf(cfg->mqtt_user, sizeof(cfg->mqtt_user), "%s", val);
        } else if (strcmp(key, "mqtt_pass") == 0) {
            snprintf(cfg->mqtt_pass, sizeof(cfg->mqtt_pass), "%s", val);
        }
    }

    fclose(f);
    printf("[config] loaded from %s\n", path);
    printf("[config] broker=%s:%d serial=%s\n",
           cfg->broker_host, cfg->broker_port, cfg->serial_path);
    return 0;
}
