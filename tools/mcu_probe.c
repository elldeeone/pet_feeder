/* MCU protocol probe - tries different feed commands on /dev/ttySAK1 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <time.h>

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

static int send_and_read(int fd, const unsigned char *cmd, int cmdlen, const char *desc) {
    unsigned char buf[256];
    ssize_t n;
    int total = 0;
    struct timespec ts;

    printf("\n--- %s ---\n  TX:", desc);
    for (int i = 0; i < cmdlen; i++) printf(" %02x", cmd[i]);
    printf("\n");

    drain(fd);
    write(fd, cmd, cmdlen);

    /* Wait up to 3 seconds for response */
    for (int w = 0; w < 30; w++) {
        ts.tv_sec = 0;
        ts.tv_nsec = 100000000; /* 100ms */
        nanosleep(&ts, NULL);
        n = read(fd, buf + total, sizeof(buf) - total);
        if (n > 0) total += n;
    }

    if (total > 0) {
        printf("  RX:");
        for (int i = 0; i < total; i++) printf(" %02x", buf[i]);
        printf(" (%d bytes)\n", total);
    } else {
        printf("  RX: (no response)\n");
    }
    return total;
}

int main(void) {
    int fd;

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("MCU Protocol Probe v1.0\n");

    fd = open_serial("/dev/ttySAK1");
    if (fd < 0) return 1;
    printf("Serial port opened OK\n");

    /* Drain any pending data */
    drain(fd);
    usleep(500000);
    drain(fd);

    /* === JKPer protocol tests (FF FF header) === */

    /* Test 1: Original BALIMO feed command */
    {
        unsigned char cmd[] = {0xFF, 0xFF, 0x01, 0x01, 0x00};
        send_and_read(fd, cmd, sizeof(cmd), "JKPer cmd=0x01 data=0x00 (BALIMO feed)");
    }
    usleep(2000000);

    /* Test 2: cmd=0x01 data=0x01 */
    {
        unsigned char cmd[] = {0xFF, 0xFF, 0x01, 0x01, 0x01};
        send_and_read(fd, cmd, sizeof(cmd), "JKPer cmd=0x01 data=0x01");
    }
    usleep(2000000);

    /* Test 3: cmd=0x02 (quick feed?) */
    {
        unsigned char cmd[] = {0xFF, 0xFF, 0x02, 0x01, 0x01};
        send_and_read(fd, cmd, sizeof(cmd), "JKPer cmd=0x02 data=0x01 (quick feed?)");
    }
    usleep(2000000);

    /* Test 4: cmd=0x03 (manual feed?) */
    {
        unsigned char cmd[] = {0xFF, 0xFF, 0x03, 0x01, 0x01};
        send_and_read(fd, cmd, sizeof(cmd), "JKPer cmd=0x03 data=0x01 (manual feed?)");
    }
    usleep(2000000);

    /* Test 5: cmd=0x04 */
    {
        unsigned char cmd[] = {0xFF, 0xFF, 0x04, 0x01, 0x01};
        send_and_read(fd, cmd, sizeof(cmd), "JKPer cmd=0x04 data=0x01");
    }
    usleep(2000000);

    /* Test 6: cmd=0x05 */
    {
        unsigned char cmd[] = {0xFF, 0xFF, 0x05, 0x01, 0x01};
        send_and_read(fd, cmd, sizeof(cmd), "JKPer cmd=0x05 data=0x01");
    }
    usleep(2000000);

    /* Test 7: cmd=0x0A */
    {
        unsigned char cmd[] = {0xFF, 0xFF, 0x0A, 0x01, 0x01};
        send_and_read(fd, cmd, sizeof(cmd), "JKPer cmd=0x0A data=0x01");
    }
    usleep(2000000);

    /* Test 8: cmd=0x0B */
    {
        unsigned char cmd[] = {0xFF, 0xFF, 0x0B, 0x01, 0x01};
        send_and_read(fd, cmd, sizeof(cmd), "JKPer cmd=0x0B data=0x01");
    }
    usleep(2000000);

    /* Test 9: cmd=0x0C (portions - what mqtt_feeder sends first) */
    {
        unsigned char cmd[] = {0xFF, 0xFF, 0x0C, 0x01, 0x01};
        send_and_read(fd, cmd, sizeof(cmd), "JKPer cmd=0x0C data=0x01 (portions?)");
    }
    usleep(2000000);

    /* === Standard Tuya MCU protocol tests (55 AA header) === */

    /* Test 10: TuyaMCU DP2 (Quick Feed) = true */
    {
        /* 55 AA 00 06 0005 02 01 0001 01 checksum */
        unsigned char cmd[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x02, 0x01, 0x00, 0x01, 0x01, 0x0F};
        send_and_read(fd, cmd, sizeof(cmd), "TuyaMCU DP2=true (Quick Feed)");
    }
    usleep(2000000);

    /* Test 11: TuyaMCU DP3 (Manual Feed) = 1 portion */
    {
        /* 55 AA 00 06 0008 03 02 0004 00000001 checksum */
        unsigned char cmd[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x08, 0x03, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x1D};
        send_and_read(fd, cmd, sizeof(cmd), "TuyaMCU DP3=1 (Manual Feed)");
    }
    usleep(2000000);

    /* Test 12: TuyaMCU heartbeat */
    {
        unsigned char cmd[] = {0x55, 0xAA, 0x00, 0x00, 0x00, 0x00, 0xFF};
        send_and_read(fd, cmd, sizeof(cmd), "TuyaMCU Heartbeat");
    }
    usleep(2000000);

    /* Test 13: TuyaMCU query product info */
    {
        unsigned char cmd[] = {0x55, 0xAA, 0x00, 0x01, 0x00, 0x00, 0x00};
        send_and_read(fd, cmd, sizeof(cmd), "TuyaMCU Query Product Info");
    }
    usleep(2000000);

    /* Test 14: TuyaMCU DP245 (Feeding Command) = 1 */
    {
        /* 55 AA 00 06 0008 F5 02 0004 00000001 checksum */
        unsigned char cmd[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x08, 0xF5, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x09};
        send_and_read(fd, cmd, sizeof(cmd), "TuyaMCU DP245=1 (Feeding Command)");
    }
    usleep(2000000);

    /* Test 15: JKPer longer packet cmd=0x01 with more data */
    {
        unsigned char cmd[] = {0xFF, 0xFF, 0x01, 0x02, 0x01, 0x00};
        send_and_read(fd, cmd, sizeof(cmd), "JKPer cmd=0x01 len=2 data=01,00");
    }
    usleep(2000000);

    /* Test 16: FF FC header (seen in response patterns) */
    {
        unsigned char cmd[] = {0xFF, 0xFC, 0x01, 0x01, 0x01};
        send_and_read(fd, cmd, sizeof(cmd), "JKPer FF FC cmd=0x01 data=0x01");
    }
    usleep(2000000);

    printf("\n=== ALL TESTS COMPLETE ===\n");
    printf("If motor ran during any test, that's the correct command.\n");

    close(fd);
    return 0;
}
