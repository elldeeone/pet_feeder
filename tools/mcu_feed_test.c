/* Test individual feed command candidates with clear pauses */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
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

static int send_and_read(int fd, const unsigned char *cmd, int cmdlen, int wait_ms) {
    unsigned char buf[256];
    ssize_t n;
    int total = 0;
    struct timespec ts;

    printf("  TX:");
    for (int i = 0; i < cmdlen; i++) printf(" %02x", cmd[i]);
    fflush(stdout);

    drain(fd);
    write(fd, cmd, cmdlen);

    int loops = wait_ms / 50;
    for (int w = 0; w < loops; w++) {
        ts.tv_sec = 0;
        ts.tv_nsec = 50000000;
        nanosleep(&ts, NULL);
        n = read(fd, buf + total, sizeof(buf) - total);
        if (n > 0) total += n;
    }

    if (total > 0) {
        printf("  RX:");
        for (int i = 0; i < total; i++) printf(" %02x", buf[i]);
        printf("\n");
    } else {
        printf("  RX: -\n");
    }
    return total;
}

int main(int argc, char *argv[]) {
    int fd;
    int test = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc > 1)
        test = atoi(argv[1]);

    fd = open_serial("/dev/ttySAK1");
    if (fd < 0) return 1;
    drain(fd);
    usleep(500000);
    drain(fd);

    /* Always do MCU init first */
    printf("=== MCU INIT ===\n");
    {unsigned char c[] = {0xFF,0xFF,0x0D,0x01,0x01}; send_and_read(fd,c,5,3000);}
    usleep(300000);
    {unsigned char c[] = {0xFF,0xFF,0x1D,0x01,0x00}; send_and_read(fd,c,5,1000);}
    usleep(300000);
    {unsigned char c[] = {0xFF,0xFF,0x11,0x01,0x02}; send_and_read(fd,c,5,1000);}
    usleep(300000);
    {unsigned char c[] = {0xFF,0xFF,0x06,0x06,0x42,0x02,0x17,0x14,0x37,0x22}; send_and_read(fd,c,10,1000);}
    usleep(1000000);
    printf("=== INIT DONE ===\n\n");

    if (test == 0 || test == 1) {
        printf(">>> TEST 1: cmd=0x0F data=0x01 (possible feed trigger) <<<\n");
        {unsigned char c[] = {0xFF,0xFF,0x0F,0x01,0x01}; send_and_read(fd,c,5,3000);}
        printf("  ^^^ Did motor spin? Wait 10s...\n");
        sleep(10);
    }

    if (test == 0 || test == 2) {
        printf(">>> TEST 2: cmd=0x10 data=0x01 <<<\n");
        {unsigned char c[] = {0xFF,0xFF,0x10,0x01,0x01}; send_and_read(fd,c,5,3000);}
        printf("  ^^^ Did motor spin? Wait 10s...\n");
        sleep(10);
    }

    if (test == 0 || test == 3) {
        printf(">>> TEST 3: cmd=0x0B data=0x01 <<<\n");
        {unsigned char c[] = {0xFF,0xFF,0x0B,0x01,0x01}; send_and_read(fd,c,5,3000);}
        printf("  ^^^ Did motor spin? Wait 10s...\n");
        sleep(10);
    }

    if (test == 0 || test == 4) {
        printf(">>> TEST 4: cmd=0x0E data=0x01 <<<\n");
        {unsigned char c[] = {0xFF,0xFF,0x0E,0x01,0x01}; send_and_read(fd,c,5,3000);}
        printf("  ^^^ Did motor spin? Wait 10s...\n");
        sleep(10);
    }

    if (test == 0 || test == 5) {
        printf(">>> TEST 5: cmd=0x30 data=0x01 <<<\n");
        {unsigned char c[] = {0xFF,0xFF,0x30,0x01,0x01}; send_and_read(fd,c,5,3000);}
        printf("  ^^^ Did motor spin? Wait 10s...\n");
        sleep(10);
    }

    if (test == 0 || test == 6) {
        /* Try setting portions to 1, then feed */
        printf(">>> TEST 6: Set portions (cmd=0x0C data=0x01) + cmd=0x0F <<<\n");
        {unsigned char c[] = {0xFF,0xFF,0x0C,0x01,0x01}; send_and_read(fd,c,5,1000);}
        usleep(200000);
        {unsigned char c[] = {0xFF,0xFF,0x0F,0x01,0x01}; send_and_read(fd,c,5,3000);}
        printf("  ^^^ Did motor spin? Wait 10s...\n");
        sleep(10);
    }

    printf("\n=== ALL TESTS DONE ===\n");
    close(fd);
    return 0;
}
