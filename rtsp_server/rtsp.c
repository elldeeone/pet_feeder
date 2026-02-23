/*
 * RTSP server for Anyka AK3918EV330 pet feeder camera
 * Based on GNCC GC2 RTSP by Piotr Gozdur (GPL v2)
 * Modified for SC223A sensor, video-only streaming
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include "ak_thread.h"
#include "ak_common.h"
#include "ak_vi.h"
#include "ak_venc.h"
#include "ak_rtsp.h"
#include "ak_vpss.h"

/* ISP config directory - contains isp_sc223a_mipi_2lane_h3b.conf */
#define ISP_CFG_PATH            "/etc/jffs2/"

#define DEFAULT_MAIN_WIDTH      1920
#define DEFAULT_MAIN_HEIGHT     1080
#define DEFAULT_SUB_WIDTH       640
#define DEFAULT_SUB_HEIGHT      360
#define DEFAULT_MAIN_FPS        25
#define DEFAULT_SUB_FPS         25
#define DEFAULT_MINQP           28
#define DEFAULT_MAXQP           42
#define DEFAULT_GOP             2
#define DEFAULT_MAIN_MODE       BR_MODE_VBR
#define DEFAULT_SUB_MODE        BR_MODE_VBR
#define DEFAULT_MAIN_TYPE       H264_ENC_TYPE
#define DEFAULT_SUB_TYPE        H264_ENC_TYPE
#define DEFAULT_MAIN_KBPS       2000
#define DEFAULT_SUB_KBPS        500
#define DEFAULT_MAIN_SUFFIX     "vs0"
#define DEFAULT_SUB_SUFFIX      "vs1"

/* Day/night switching */
enum day_night_switch_mode {
    SET_NIGHT_MODE,
    SET_DAY_MODE,
    SET_AUTO_MODE,
    SET_CLOSE_MODE
};

enum ak_photosensitive_mode {
    HARDWARE_PHOTOSENSITIVE,
    AUTO_PHOTOSENSITIVE
};

struct ak_misc {
    int ircut_run_flag;
    int pre_status;
    void *vi_handle;
    ak_pthread_t ircut_tid;
    int cur_set_mode;
    int thread_run;
    enum ak_photosensitive_mode ps_mode;
    thread_func ps_fun_thread;
};

static struct ak_misc misc_ctrl = {0};
static int run_flag = AK_FALSE;

static int switch_day_or_night(enum mode_state mode)
{
    if (misc_ctrl.vi_handle == 0)
        return AK_FAILED;

    int fd_ir_cut_a = open("/sys/user-gpio/gpio-ircut_a", O_WRONLY);
    int fd_ir_cut_b = open("/sys/user-gpio/gpio-ircut_b", O_WRONLY);
    int fd_ir_led = open("/sys/user-gpio/ir-led", O_WRONLY);

    int ret = AK_FAILED;
    if (mode == STATE_DAY) {
        ak_print_normal_ex("set to day\n");
        if (fd_ir_cut_a >= 0) write(fd_ir_cut_a, "0", 1);
        if (fd_ir_cut_b >= 0) { write(fd_ir_cut_b, "1", 1); usleep(100000); write(fd_ir_cut_b, "0", 1); }
        if (fd_ir_led >= 0) write(fd_ir_led, "0", 1);
        ret = ak_vi_switch_mode(misc_ctrl.vi_handle, VI_MODE_DAY);
    } else {
        ak_print_normal_ex("set to night\n");
        ret = ak_vi_switch_mode(misc_ctrl.vi_handle, VI_MODE_NIGHT);
        if (fd_ir_led >= 0) write(fd_ir_led, "1", 1);
        if (fd_ir_cut_b >= 0) write(fd_ir_cut_b, "0", 1);
        if (fd_ir_cut_a >= 0) { write(fd_ir_cut_a, "1", 1); usleep(100000); write(fd_ir_cut_a, "0", 1); }
    }

    if (fd_ir_cut_a >= 0) close(fd_ir_cut_a);
    if (fd_ir_cut_b >= 0) close(fd_ir_cut_b);
    if (fd_ir_led >= 0) close(fd_ir_led);

    return ret;
}

static void *ir_auto_switch_th(void *arg)
{
    (void)arg;
    long int tid = ak_thread_get_tid();
    int cur_status = -1;

    switch_day_or_night(STATE_DAY);

    ak_print_normal_ex("IR auto-switch thread start, id: %ld\n", tid);
    ak_thread_set_name("IR_switch");

    misc_ctrl.thread_run = AK_TRUE;

    while (misc_ctrl.thread_run) {
        if (misc_ctrl.cur_set_mode != SET_AUTO_MODE)
            break;

        cur_status = ak_vpss_isp_get_auto_day_night_level(misc_ctrl.pre_status);
        if (AK_FAILED == cur_status) {
            ak_sleep_ms(1000);
            continue;
        }

        if (misc_ctrl.pre_status != cur_status) {
            misc_ctrl.pre_status = cur_status;
            switch_day_or_night(cur_status == STATE_DAY ? STATE_DAY : STATE_NIGHT);
        }
        ak_sleep_ms(2000);
    }

    ak_print_normal_ex("IR auto-switch thread exit, id: %ld\n", tid);
    ak_thread_exit();
    return NULL;
}

static void init_day_night(void)
{
    struct ak_auto_day_night_threshold threshold;
    threshold.day_to_night_lum = 3584;
    threshold.night_to_day_lum = 1536;
    threshold.lock_time = 900000;

    int i;
    for (i = 0; i < 5; i++) threshold.night_cnt[i] = 15000;
    for (i = 0; i < 10; i++) threshold.day_cnt[i] = 300000;

    ak_vpss_isp_set_auto_day_night_param(&threshold);

    misc_ctrl.vi_handle = ak_vi_get_handle(VIDEO_DEV0);
    misc_ctrl.ircut_run_flag = AK_TRUE;
    misc_ctrl.cur_set_mode = SET_AUTO_MODE;
    misc_ctrl.ps_mode = AUTO_PHOTOSENSITIVE;
    misc_ctrl.pre_status = STATE_DAY;
    misc_ctrl.ps_fun_thread = ir_auto_switch_th;

    ak_thread_create(&misc_ctrl.ircut_tid, misc_ctrl.ps_fun_thread, NULL, 100 * 1024, -1);
}

/* Stub functions required by some SDK libs */
int sys_time_string(void) { return 0; }
int log_write_to_buff(void) { return 0; }

static void *ak_rtsp_vi_init(void)
{
    if (ak_vi_match_sensor(ISP_CFG_PATH) < 0) {
        ak_print_error_ex("match sensor failed\n");
        return NULL;
    }

    void *handle = ak_vi_open(VIDEO_DEV0);
    if (handle == NULL) {
        ak_print_error_ex("vi open failed\n");
        return NULL;
    }

    struct video_resolution resolution = {0};
    if (ak_vi_get_sensor_resolution(handle, &resolution)) {
        ak_print_error_ex("get sensor resolution failed\n");
        ak_vi_close(handle);
        return NULL;
    }
    ak_print_normal("sensor resolution: %dx%d\n", resolution.width, resolution.height);

    struct video_channel_attr attr;
    memset(&attr, 0, sizeof(struct video_channel_attr));

    attr.crop.left = 0;
    attr.crop.top = 0;
    attr.crop.width = resolution.width;
    attr.crop.height = resolution.height;

    attr.res[VIDEO_CHN_MAIN].width = DEFAULT_MAIN_WIDTH;
    attr.res[VIDEO_CHN_MAIN].height = DEFAULT_MAIN_HEIGHT;
    attr.res[VIDEO_CHN_MAIN].max_width = 1920;
    attr.res[VIDEO_CHN_MAIN].max_height = 1080;

    attr.res[VIDEO_CHN_SUB].width = DEFAULT_SUB_WIDTH;
    attr.res[VIDEO_CHN_SUB].height = DEFAULT_SUB_HEIGHT;
    attr.res[VIDEO_CHN_SUB].max_width = 640;
    attr.res[VIDEO_CHN_SUB].max_height = 360;

    if (ak_vi_set_channel_attr(handle, &attr)) {
        ak_print_error_ex("set channel attribute failed\n");
        ak_vi_close(handle);
        return NULL;
    }

    if (ak_vi_capture_on(handle)) {
        ak_print_error_ex("start capture failed\n");
        ak_vi_close(handle);
        return NULL;
    }

    ak_print_notice("capture started\n");
    return handle;
}

static void process_signal(unsigned int sig, siginfo_t *si, void *ptr)
{
    ak_backtrace(sig, si, ptr);
    run_flag = AK_FALSE;
}

static void register_signal(void)
{
    struct sigaction s;
    s.sa_flags = SA_SIGINFO;
    s.sa_sigaction = (void *)process_signal;

    sigaction(SIGSEGV, &s, NULL);
    sigaction(SIGINT, &s, NULL);
    sigaction(SIGTERM, &s, NULL);
    sigaction(SIGUSR1, &s, NULL);
    sigaction(SIGALRM, &s, NULL);
    sigaction(SIGHUP, &s, NULL);
    sigaction(SIGPIPE, &s, NULL);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
}

int main(int argc, char **argv)
{
    void *vi_handle;
    int ret;

    (void)argc;
    (void)argv;

    printf("pet_feeder_rtsp v1.0 - RTSP server for pet feeder camera\n");

    register_signal();

    vi_handle = ak_rtsp_vi_init();
    if (vi_handle == NULL) {
        ak_print_error("vi init failed\n");
        return 1;
    }
    ak_print_notice("vi init ok\n");

    /* Flip image if camera is mounted upside down */
    ak_vi_set_flip_mirror(vi_handle, 1, 1);

    /* Start auto day/night IR switching */
    init_day_night();

    /* Configure RTSP channels */
    struct rtsp_param param = {{{0}}};

    /* Main channel - 1080p H264 */
    param.rtsp_chn[0].current_channel = 0;
    param.rtsp_chn[0].width     = DEFAULT_MAIN_WIDTH;
    param.rtsp_chn[0].height    = DEFAULT_MAIN_HEIGHT;
    param.rtsp_chn[0].fps       = DEFAULT_MAIN_FPS;
    param.rtsp_chn[0].max_kbps  = DEFAULT_MAIN_KBPS;
    param.rtsp_chn[0].min_qp    = DEFAULT_MINQP;
    param.rtsp_chn[0].max_qp    = DEFAULT_MAXQP;
    param.rtsp_chn[0].gop_len   = DEFAULT_GOP;
    param.rtsp_chn[0].video_enc_type = DEFAULT_MAIN_TYPE;
    param.rtsp_chn[0].video_br_mode  = DEFAULT_MAIN_MODE;
    param.rtsp_chn[0].vi_handle = vi_handle;
    strcpy(param.rtsp_chn[0].suffix_name, DEFAULT_MAIN_SUFFIX);

    /* Sub channel - 360p H264 */
    param.rtsp_chn[1].current_channel = 1;
    param.rtsp_chn[1].width     = DEFAULT_SUB_WIDTH;
    param.rtsp_chn[1].height    = DEFAULT_SUB_HEIGHT;
    param.rtsp_chn[1].fps       = DEFAULT_SUB_FPS;
    param.rtsp_chn[1].max_kbps  = DEFAULT_SUB_KBPS;
    param.rtsp_chn[1].min_qp    = DEFAULT_MINQP;
    param.rtsp_chn[1].max_qp    = DEFAULT_MAXQP;
    param.rtsp_chn[1].gop_len   = DEFAULT_GOP;
    param.rtsp_chn[1].video_enc_type = DEFAULT_SUB_TYPE;
    param.rtsp_chn[1].video_br_mode  = DEFAULT_SUB_MODE;
    param.rtsp_chn[1].vi_handle = vi_handle;
    strcpy(param.rtsp_chn[1].suffix_name, DEFAULT_SUB_SUFFIX);

    ret = ak_rtsp_init(&param);
    if (ret) {
        ak_print_error("rtsp init failed\n");
        return 1;
    }
    ak_print_notice("rtsp init ok\n");

    ak_rtsp_start(VIDEO_CHN_MAIN);
    ak_rtsp_start(VIDEO_CHN_SUB);

    printf("RTSP streams:\n");
    printf("  Main (1080p): rtsp://<ip>:554/%s\n", DEFAULT_MAIN_SUFFIX);
    printf("  Sub  (360p):  rtsp://<ip>:554/%s\n", DEFAULT_SUB_SUFFIX);

    run_flag = AK_TRUE;
    while (run_flag) {
        ak_sleep_ms(1000);
    }

    printf("shutting down RTSP...\n");
    misc_ctrl.thread_run = AK_FALSE;
    ak_rtsp_stop(VIDEO_CHN_MAIN);
    ak_rtsp_stop(VIDEO_CHN_SUB);
    ak_rtsp_exit();
    ak_vi_capture_off(vi_handle);
    ak_vi_close(vi_handle);

    return 0;
}
