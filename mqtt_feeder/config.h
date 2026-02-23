#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_PATH "/mnt/config/mqtt_feeder.conf"

#define DEFAULT_BROKER_HOST "localhost"
#define DEFAULT_BROKER_PORT 1883
#define DEFAULT_SERIAL_PATH "/dev/ttySAK1"

struct config {
    char broker_host[256];
    int broker_port;
    char serial_path[128];
    char mqtt_user[128];
    char mqtt_pass[128];
};

/* Load config from file. Missing keys use defaults. Returns 0 on success. */
int config_load(struct config *cfg, const char *path);

#endif
