#ifndef FEEDER_H
#define FEEDER_H

#include <time.h>

/* Feeder state machine states */
enum feeder_state {
    FEEDER_IDLE,
    FEEDER_FEEDING,      /* motor running, waiting for MCU done response */
    FEEDER_WAIT_NEXT     /* waiting before next portion */
};

/* Max time to wait for MCU response before giving up (seconds) */
#define FEED_TIMEOUT_SEC 30

struct feeder {
    int serial_fd;
    enum feeder_state state;
    int portions_target;    /* total portions to dispense */
    int portions_done;      /* portions dispensed so far */
    time_t last_feed_time;  /* timestamp of last command sent */
    unsigned char rxbuf[32]; /* serial receive buffer */
    int rxlen;               /* bytes in rxbuf */
};

/* Open serial port at 9600 8N1. Returns 0 on success. */
int feeder_open(struct feeder *f, const char *serial_path);

/* Close serial port */
void feeder_close(struct feeder *f);

/* Trigger N portions (1-10). Returns 0 if accepted. */
int feeder_feed(struct feeder *f, int portions);

/* Call from main loop. Returns 1 if state changed. */
int feeder_tick(struct feeder *f);

/* Returns 1 if currently dispensing */
int feeder_is_busy(struct feeder *f);

#endif
