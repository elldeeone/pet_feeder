/* MCU probe v2 - expanded command search + initialization replay */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>

static int open_serial(const char *path) {
    struct termios tty;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return -1; }
    memset(&tty, 0, sizeof(tty));
    tcgetattr(fd, &tty);
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

static void drain(int fd) {
    unsigned char buf[256];
    while (read(fd, buf, sizeof(buf)) > 0) {}
}

static int send_and_read(int fd, const unsigned char *cmd, int cmdlen, const char *desc, int wait_ms) {
    unsigned char buf[256];
    ssize_t n;
    int total = 0;
    struct timespec ts;

    printf("  %s  TX:", desc);
    for (int i = 0; i < cmdlen; i++) printf(" %02x", cmd[i]);
    fflush(stdout);

    drain(fd);
    write(fd, cmd, cmdlen);

    int loops = wait_ms / 50;
    for (int w = 0; w < loops; w++) {
        ts.tv_sec = 0;
        ts.tv_nsec = 50000000; /* 50ms */
        nanosleep(&ts, NULL);
        n = read(fd, buf + total, sizeof(buf) - total);
        if (n > 0) total += n;
    }

    if (total > 0) {
        printf("  RX:");
        for (int i = 0; i < total; i++) printf(" %02x", buf[i]);
        printf(" (%d)\n", total);
    } else {
        printf("  RX: -\n");
    }
    return total;
}

int main(int argc, char *argv[]) {
    int fd;
    int mode = 0; /* 0=scan, 1=init_then_scan */

    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc > 1 && strcmp(argv[1], "init") == 0)
        mode = 1;

    fd = open_serial("/dev/ttySAK1");
    if (fd < 0) return 1;
    printf("MCU Probe v2 - mode=%s\n", mode ? "init+scan" : "scan");

    drain(fd);
    usleep(500000);
    drain(fd);

    if (mode == 1) {
        printf("\n=== REPLAYING STOCK INIT SEQUENCE ===\n");

        /* Step 1: Get MCU version (cmd=0x0D) */
        {
            unsigned char cmd[] = {0xFF, 0xFF, 0x0D, 0x01, 0x01};
            send_and_read(fd, cmd, sizeof(cmd), "GET_VERSION", 3000);
        }
        usleep(500000);

        /* Step 2: Init (cmd=0x1D) */
        {
            unsigned char cmd[] = {0xFF, 0xFF, 0x1D, 0x01, 0x00};
            send_and_read(fd, cmd, sizeof(cmd), "INIT_1D    ", 1000);
        }
        usleep(500000);

        /* Step 3: LED on (cmd=0x11) */
        {
            unsigned char cmd[] = {0xFF, 0xFF, 0x11, 0x01, 0x02};
            send_and_read(fd, cmd, sizeof(cmd), "LED_ON     ", 1000);
        }
        usleep(500000);

        /* Step 4: cmd=0x32 config query */
        {
            unsigned char cmd[] = {0xFF, 0xFF, 0x32, 0x01, 0x02};
            send_and_read(fd, cmd, sizeof(cmd), "CFG_QUERY  ", 1000);
        }
        usleep(500000);

        /* Step 5: Time sync (cmd=0x06) - use current-ish time */
        {
            unsigned char cmd[] = {0xFF, 0xFF, 0x06, 0x06, 0x42, 0x02, 0x17, 0x14, 0x37, 0x22};
            send_and_read(fd, cmd, sizeof(cmd), "TIME_SYNC  ", 1000);
        }
        usleep(500000);

        printf("\n=== INIT DONE, NOW SCANNING FEED COMMANDS ===\n");
    }

    /* Scan all cmd bytes 0x00 - 0x40 with data=0x01 */
    printf("\n=== SCANNING cmds 0x00-0x40 data=0x01 ===\n");
    for (int c = 0; c <= 0x40; c++) {
        /* Skip known non-feed commands to save time */
        if (c == 0x06 || c == 0x0D || c == 0x11 || c == 0x1D || c == 0x32)
            continue;

        unsigned char cmd[5] = {0xFF, 0xFF, (unsigned char)c, 0x01, 0x01};
        char desc[32];
        snprintf(desc, sizeof(desc), "cmd=0x%02x   ", c);
        send_and_read(fd, cmd, sizeof(cmd), desc, 1500);
        usleep(500000);
    }

    /* Also try commands with data=0x00 for commonly-found responsive cmds */
    printf("\n=== TRYING SOME WITH data=0x00 ===\n");
    for (int c = 0x10; c <= 0x20; c++) {
        unsigned char cmd[5] = {0xFF, 0xFF, (unsigned char)c, 0x01, 0x00};
        char desc[32];
        snprintf(desc, sizeof(desc), "cmd=0x%02x/0 ", c);
        send_and_read(fd, cmd, sizeof(cmd), desc, 1500);
        usleep(500000);
    }

    printf("\n=== ALL TESTS COMPLETE ===\n");
    printf("If motor ran during any test, note the command.\n");

    close(fd);
    return 0;
}
