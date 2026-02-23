#include "feeder.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

/* Brief pause between portions to let MCU settle (seconds) */
#define PORTION_GAP_SEC 1

/* Check for MCU response - returns 1 if a complete response was received */
static int check_mcu_response(struct feeder *f) {
    unsigned char buf[64];
    ssize_t n;
    int i;

    n = read(f->serial_fd, buf, sizeof(buf));
    if (n <= 0)
        return 0;

    printf("[feeder] MCU rx %zd bytes:", n);
    for (i = 0; i < n; i++)
        printf(" %02x", buf[i]);
    printf("\n");

    /* Look for response with FF FF or FF FC header */
    for (i = 0; i < n; i++) {
        if (f->rxlen < (int)sizeof(f->rxbuf))
            f->rxbuf[f->rxlen++] = buf[i];
    }

    for (i = 0; i < f->rxlen - 1; i++) {
        if (f->rxbuf[i] == 0xFF && (f->rxbuf[i+1] == 0xFF || f->rxbuf[i+1] == 0xFC)) {
            printf("[feeder] MCU response detected at offset %d\n", i);
            f->rxlen = 0;
            return 1;
        }
    }

    return 0;
}

static void drain_serial(int fd) {
    unsigned char buf[64];
    while (read(fd, buf, sizeof(buf)) > 0) {}
}

static int send_raw(int fd, const unsigned char *pkt, int len) {
    ssize_t n = write(fd, pkt, len);
    if (n != len) {
        printf("[feeder] write failed: %s\n", strerror(errno));
        return -1;
    }
    printf("[feeder] TX:");
    for (int i = 0; i < len; i++) printf(" %02x", pkt[i]);
    printf("\n");
    return 0;
}

static int send_cmd(int fd, unsigned char cmd, unsigned char data) {
    unsigned char pkt[5] = { 0xFF, 0xFF, cmd, 0x01, data };
    return send_raw(fd, pkt, 5);
}

/* Send the MCU initialization sequence (same as stock binary) */
static void mcu_init(int fd) {
    unsigned char buf[64];
    int i;
    ssize_t n;

    printf("[feeder] MCU init sequence...\n");
    drain_serial(fd);

    /* Step 1: Query MCU version (cmd=0x0D) */
    send_cmd(fd, 0x0D, 0x01);
    usleep(500000);
    n = read(fd, buf, sizeof(buf));
    if (n > 0) {
        printf("[feeder] MCU version response %zd bytes:", n);
        for (i = 0; i < n; i++) printf(" %02x", buf[i]);
        printf("\n");
        /* Extract version string (starts at byte 4) */
        if (n > 4) {
            printf("[feeder] MCU version: ");
            for (i = 4; i < n; i++) {
                if (buf[i] >= 0x20 && buf[i] < 0x7F)
                    putchar(buf[i]);
            }
            printf("\n");
        }
    }

    /* Step 2: Init command (cmd=0x1D) */
    send_cmd(fd, 0x1D, 0x00);
    usleep(300000);
    drain_serial(fd);

    /* Step 3: LED on (cmd=0x11 data=0x02) */
    send_cmd(fd, 0x11, 0x02);
    usleep(300000);
    drain_serial(fd);

    /* Step 4: Time sync (cmd=0x06, 6 bytes of time data) */
    {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        unsigned char ts_pkt[10] = {
            0xFF, 0xFF, 0x06, 0x06,
            (unsigned char)(t->tm_year - 100), /* years since 2000 */
            (unsigned char)(t->tm_mon + 1),
            (unsigned char)t->tm_mday,
            (unsigned char)t->tm_hour,
            (unsigned char)t->tm_min,
            (unsigned char)t->tm_sec
        };
        send_raw(fd, ts_pkt, 10);
    }
    usleep(300000);
    drain_serial(fd);

    printf("[feeder] MCU init complete\n");
}

int feeder_open(struct feeder *f, const char *serial_path) {
    struct termios tty;

    memset(f, 0, sizeof(*f));
    f->serial_fd = -1;
    f->state = FEEDER_IDLE;

    f->serial_fd = open(serial_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (f->serial_fd < 0) {
        printf("[feeder] failed to open %s: %s\n", serial_path, strerror(errno));
        return -1;
    }

    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(f->serial_fd, &tty) != 0) {
        printf("[feeder] tcgetattr failed: %s\n", strerror(errno));
        close(f->serial_fd);
        f->serial_fd = -1;
        return -1;
    }

    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(f->serial_fd, TCSANOW, &tty) != 0) {
        printf("[feeder] tcsetattr failed: %s\n", strerror(errno));
        close(f->serial_fd);
        f->serial_fd = -1;
        return -1;
    }

    printf("[feeder] opened %s at 9600 8N1\n", serial_path);

    /* Initialize MCU (version query, LED, time sync) */
    mcu_init(f->serial_fd);

    return 0;
}

void feeder_close(struct feeder *f) {
    if (f->serial_fd >= 0) {
        close(f->serial_fd);
        f->serial_fd = -1;
    }
}

static int send_feed_command(struct feeder *f) {
    /* cmd=0x0F triggers one motor cycle (one portion) */
    int rc = send_cmd(f->serial_fd, 0x0F, 0x01);
    printf("[feeder] portion %d/%d dispensed\n",
           f->portions_done + 1, f->portions_target);
    return rc;
}

int feeder_feed(struct feeder *f, int portions) {
    if (f->serial_fd < 0) {
        printf("[feeder] serial port not open\n");
        return -1;
    }
    if (f->state != FEEDER_IDLE) {
        printf("[feeder] busy, ignoring request\n");
        return -1;
    }
    if (portions < 1) portions = 1;
    if (portions > 10) portions = 10;

    f->portions_target = portions;
    f->portions_done = 0;

    if (send_feed_command(f) < 0)
        return -1;

    f->portions_done = 1;
    f->last_feed_time = time(NULL);
    f->state = FEEDER_FEEDING;
    f->rxlen = 0;

    return 0;
}

int feeder_tick(struct feeder *f) {
    time_t now;

    if (f->state == FEEDER_IDLE) {
        unsigned char drain[32];
        read(f->serial_fd, drain, sizeof(drain));
        return 0;
    }

    now = time(NULL);

    if (f->state == FEEDER_FEEDING) {
        int got_response = check_mcu_response(f);
        int timed_out = (now - f->last_feed_time >= FEED_TIMEOUT_SEC);

        if (got_response || timed_out) {
            if (timed_out)
                printf("[feeder] timeout waiting for MCU response\n");

            if (f->portions_done >= f->portions_target) {
                printf("[feeder] feed complete (%d/%d portions)\n",
                       f->portions_done, f->portions_target);
                f->state = FEEDER_IDLE;
                return 1;
            }

            /* More portions needed — wait before next */
            printf("[feeder] portion %d/%d done, waiting %ds before next\n",
                   f->portions_done, f->portions_target, PORTION_GAP_SEC);
            f->last_feed_time = now;
            f->state = FEEDER_WAIT_NEXT;
        }
        return 0;
    }

    if (f->state == FEEDER_WAIT_NEXT) {
        if (now - f->last_feed_time >= PORTION_GAP_SEC) {
            if (send_feed_command(f) < 0) {
                printf("[feeder] send failed, aborting at %d/%d\n",
                       f->portions_done, f->portions_target);
                f->state = FEEDER_IDLE;
                return 1;
            }
            f->portions_done++;
            f->last_feed_time = now;
            f->state = FEEDER_FEEDING;
            f->rxlen = 0;
        }
        return 0;
    }

    return 0;
}

int feeder_is_busy(struct feeder *f) {
    return f->state != FEEDER_IDLE;
}
