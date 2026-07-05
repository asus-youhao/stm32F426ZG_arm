/**
 * @file    pc_master_main.c
 * @brief   PC 端 CANopen 主站 — WP-H2：harness + loop engine 驅動（雙執行緒）
 *
 * 架構（docs/design/harness-agent-loop-engine-plan.md §2/§5）：
 *   RT 執行緒   ：loop engine（clock_nanosleep 絕對時間）+ 四控制 agent
 *                 + Command/Telemetry agent。RT 路徑零 printf/零 stdin。
 *   主執行緒    ：harness——stdin 解析→cmd ring、telemetry ring→狀態列印、
 *                 hn_supervise 心跳監督（engine 卡死→NMT stop 最後防線）。
 *
 * 相對舊版（單執行緒、tick 內 printf/select）的差異即 H2 驗收：
 *   stdout 被塞住（`| pv -L 1`）不影響 tick;--rate 執行期切換 400/500 Hz。
 */
#include "canopen.h"
#include "co_nmt.h"
#include "co_bxcan_socketcan.h"
#include "dual_arm.h"
#include "dual_arm_ctrl.h"
#include "task_space.h"
#include "control_rate.h"
#include "bringup.h"
#include "harness.h"
#include "app_agents.h"
#include "app_io_agents.h"
#include "eng_log.h"
#include "eng_trace.h"
#include "safety.h"
#include "stm32f7xx_hal.h"

#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

/* app_main.c 對外 API（無公用標頭,與 board/main.c 同樣以 extern 取用） */
void app_main_init_hz(float hz);
const char *app_sys_state(void);
void app_select_bus(const bus_if_t *bus);
int  app_present_count(void);
extern const bus_if_t g_bus_ecat;

/* bringup.c 的弱連結 log → 導到 stdout */
void bringup_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static volatile sig_atomic_t s_quit = 0;
static void on_sigint(int sig) { (void)sig; s_quit = 1; }

static void usage(const char *argv0)
{
    printf("用法: %s [--bus canopen|ethercat] [--left IF] [--right IF|none] [--rate HZ] [--sync] [--trace FILE] [--bringup NODE] [--seconds N]\n"
           "  --bus B        協定（預設 canopen;ethercat 目前接 sim 後端=SIL）\n"
           "  --left IF      左臂 SocketCAN 介面（預設 vcan0）\n"
           "  --right IF     右臂 SocketCAN 介面（預設 vcan1;'none' 停用 → 單臂）\n"
           "  --rate HZ      控制頻率（100..1000,預設 %u;WP-C 檔位 400/500）\n"
           "  --sync         SYNC 同步鎖存模式（transmission type=1,G3）\n"
           "  --trace FILE   每 tick 抖動紀錄→CSV（WP-H3;離線用 tools/trace_report.py）\n"
           "  --bringup N    先對左臂 node N 跑 WP2 單軸 bring-up\n"
           "  --seconds N    跑 N 秒後自動結束（0=直到 Ctrl-C）\n"
           "互動命令（stdin）：\n"
           "  j <idx> <rad>  關節點到點（idx 0..13）\n"
           "  e <0|1>        急停 off/on\n"
           "  p              印出雙臂末端位姿（讀最新遙測快照）\n"
           "  q              離開\n", argv0, (unsigned)CONTROL_HZ);
}

/* ================= RT 執行緒：engine 驅動 ================= */

static volatile int s_rt_stop = 0;

static void *rt_thread_fn(void *arg)
{
    loop_engine_t *e = arg;

    /* RT 排程與鎖頁：盡力而為,非 root/非 RT 內核下降級運行並警告一次
       （抖動驗收屬 WP-H3,在 PREEMPT_RT 機上以 root/rtprio 權限跑） */
    struct sched_param sp = { .sched_priority = 80 };
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
        fprintf(stderr, "[rt] 警告：SCHED_FIFO 失敗（無權限?）,以一般排程降級運行\n");

    while (!s_rt_stop && !s_quit) {
        uint64_t dl = eng_next_deadline_us(e);
        struct timespec ts = { (time_t)(dl / 1000000u),
                               (long)(dl % 1000000u) * 1000L };
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        eng_tick(e);
    }
    return NULL;
}

/* ============ harness（主執行緒,非 RT）============ */

/* 最後防線：engine 心跳停滯/連續 overrun 時由 harness 直接動作。
   先設急停旗標（若 RT 還在動,safety 會下 quick stop）,
   再直接對兩條 bus 廣播 NMT stop（RT 已死也停得下來）。 */
static void enter_safe_stop_cb(void *user)
{
    (void)user;
    safety_set_estop(true);
    co_nmt_send(CO_BUS_LEFT,  CO_NMT_STOP, 0);
    co_nmt_send(CO_BUS_RIGHT, CO_NMT_STOP, 0);
    fprintf(stderr, "[harness] SAFE_STOP：estop + NMT stop 已下發\n");
}

/* trace ring 排水：pop → CSV 一行（主執行緒;buffered stdio,離 RT 路徑） */
static void trace_drain(FILE *tf)
{
    eng_trace_rec_t tr;
    if (!tf) return;
    while (eng_trace_pop(&tr))
        fprintf(tf, "%llu,%lu,%u,%u,%u,%u,%u\n",
                (unsigned long long)tr.t_us, (unsigned long)tr.late_us,
                tr.ph_us[ENG_PH_READ], tr.ph_us[ENG_PH_COMPUTE],
                tr.ph_us[ENG_PH_WRITE], tr.ph_us[ENG_PH_HOUSE], tr.flags);
}

/* 非阻塞讀 stdin 一行 */
static bool poll_stdin(char *line, size_t cap)
{
    fd_set rf;
    struct timeval tv = { 0, 0 };
    FD_ZERO(&rf);
    FD_SET(STDIN_FILENO, &rf);
    if (select(STDIN_FILENO + 1, &rf, NULL, NULL, &tv) <= 0) return false;
    if (!fgets(line, (int)cap, stdin)) { s_quit = 1; return false; }
    return true;
}

/* 命令解析：只做解析與 ring push,套用在 RT 域（CommandAgent） */
static void handle_cmd(const char *line, const app_tele_t *last)
{
    int idx, v;
    float rad;
    app_cmd_t c;
    if (sscanf(line, "j %d %f", &idx, &rad) == 2 && idx >= 0 && idx < 14) {
        c = (app_cmd_t){ .op = APP_CMD_JOINT_MOVE, .idx = (uint8_t)idx, .val = rad };
        printf(app_io_cmd_push(&c) ? "  → J%d move_to %.3f rad\n"
                                   : "  ！cmd ring 滿,丟棄（J%d %.3f）\n", idx, rad);
    } else if (sscanf(line, "e %d", &v) == 1) {
        /* 操作員急停走 RT 域 safety（quick stop,可用 e 0 復歸）;
           harness 的 SAFE_STOP（NMT stop,不可逆）只留給監督觸發 */
        c = (app_cmd_t){ .op = APP_CMD_ESTOP, .val = (float)v };
        app_io_cmd_push(&c);
        printf("  → estop %s\n", v ? "ON" : "OFF");
    } else if (line[0] == 'p') {
        printf("  左末端 xyz=(%.3f, %.3f, %.3f)  右末端 xyz=(%.3f, %.3f, %.3f)\n",
               last->lpos[0], last->lpos[1], last->lpos[2],
               last->rpos[0], last->rpos[1], last->rpos[2]);
    } else if (line[0] == 'q') {
        s_quit = 1;
    } else if (line[0] != '\n') {
        printf("  未知命令（j/e/p/q）\n");
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *left = "vcan0", *right = "vcan1";
    const char *bus = "canopen";
    const char *trace_path = NULL;
    int bringup_node = 0;
    long run_seconds = 0;
    long rate_hz = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--left") && i + 1 < argc)         left = argv[++i];
        else if (!strcmp(argv[i], "--right") && i + 1 < argc)   right = argv[++i];
        else if (!strcmp(argv[i], "--bus") && i + 1 < argc)     bus = argv[++i];
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc)    rate_hz = atol(argv[++i]);
        else if (!strcmp(argv[i], "--sync"))                    dual_arm_set_sync(true);
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc)   trace_path = argv[++i];
        else if (!strcmp(argv[i], "--bringup") && i + 1 < argc) bringup_node = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) run_seconds = atol(argv[++i]);
        else { usage(argv[0]); return (strcmp(argv[i], "--help") == 0) ? 0 : 2; }
    }
    if (!strcmp(right, "none")) right = "";

    /* WP-H5：同一 binary 切協定。ethercat 目前連結 fake 後端（SIL）,
       真後端（SOEM/IgH）之後以同一 ec_master.h 門面替換。 */
    bool is_ecat = (strcmp(bus, "ethercat") == 0);
    if (!is_ecat && strcmp(bus, "canopen") != 0) {
        fprintf(stderr, "--bus 只支援 canopen|ethercat\n");
        return 2;
    }
    if (is_ecat) app_select_bus(&g_bus_ecat);
    if (rate_hz == 0) rate_hz = is_ecat ? 1000 : (long)CONTROL_HZ;
    if (rate_hz < 100 || rate_hz > 1000) {
        fprintf(stderr, "--rate 需在 100..1000（Classic CAN 上限 500）\n");
        return 2;
    }

    signal(SIGINT, on_sigint);
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
        fprintf(stderr, "[rt] 警告：mlockall 失敗（無權限?）\n");

    co_socketcan_set_ifname(CO_BUS_LEFT, left);
    co_socketcan_set_ifname(CO_BUS_RIGHT, right);

    printf("=== PC 主站（harness + loop engine, WP-H2/H4/H5）===\n");
    if (is_ecat)
        printf("bus=ethercat(sim 後端)  rate=%ld Hz\n", rate_hz);
    else
        printf("bus=canopen  左臂=%s  右臂=%s  rate=%ld Hz  sync=%s\n",
               left, right[0] ? right : "(停用)", rate_hz,
               dual_arm_sync_enabled() ? "on" : "off");

    /* （選配）WP2 單軸 bring-up（BUS_UP 前的自檢,非 RT、可阻塞;CANopen 限定） */
    if (bringup_node > 0 && !is_ecat) {
        bringup_report_t rep;
        co_status_t st = bringup_single_axis(CO_BUS_LEFT, (uint8_t)bringup_node,
                                             5000, 1000, &rep);
        printf("=== bring-up result = %d (0=OK) ===\n", st);
        printf("  pos_before=%ld pos_after=%ld moved=%d\n",
               (long)rep.pos_before, (long)rep.pos_after, rep.moved);
    }

    /* BUS_UP：L1–L4 全棧初始化（SDO 往返、可阻塞 → 在 harness 執行緒做） */
    printf("\n=== harness: BUS_UP（init L1-L4 stack）===\n");
    app_main_init_hz((float)rate_hz);
    int present = app_present_count();
    printf("present joints = %d / 14%s\n", present,
           present ? "" : "  (init FAILED — 從站沒起來?)");
    if (present == 0) return 1;

    /* engine + agents + harness */
    loop_engine_t eng;
    harness_t hn;
    eng_cfg_t ecfg = {
        .dt_us = (uint32_t)(1000000L / rate_hz),
        .user = &hn,
        .on_escalate = hn_notify_escalation,
    };
    eng_init(&eng, &ecfg);
    if (app_agents_register(&eng) || app_io_register(&eng)) {
        fprintf(stderr, "agent 註冊失敗\n");
        return 1;
    }
    /* trace ring（WP-H3 儀器）：RT 端每 tick 一筆,主執行緒排水成 CSV。
       8192 筆 ≈ 1 kHz 下 8 s 緩衝,排水週期 20 ms 綽綽有餘。 */
    static eng_trace_rec_t trace_mem[8192];
    FILE *tf = NULL;
    if (trace_path) {
        tf = fopen(trace_path, "w");
        if (!tf) { fprintf(stderr, "--trace 開檔失敗：%s\n", trace_path); return 1; }
        fprintf(tf, "t_us,late_us,read_us,compute_us,write_us,house_us,flags\n");
        eng_trace_init(trace_mem, 8192);
    }

    hn_cfg_t hcfg = { .stall_checks = 3, .enter_safe_stop = enter_safe_stop_cb };
    hn_init(&hn, &eng, &hcfg);
    if (hn_configure(&hn) || hn_activate(&hn)) {
        fprintf(stderr, "harness configure/activate 失敗\n");
        return 1;
    }

    pthread_t rt;
    if (pthread_create(&rt, NULL, rt_thread_fn, &eng) != 0) {
        fprintf(stderr, "RT 執行緒建立失敗\n");
        return 1;
    }

    printf("RUN（Ctrl-C 或 'q' 結束）。命令：j <idx> <rad> / e <0|1> / p / q\n");

    /* 主執行緒：50 Hz 輪詢 stdin + 遙測列印 + 10 Hz 監督 */
    app_tele_t tele = {0}, t;
    app_health_t hl[CO_BUS_COUNT] = {0}, hrec;
    char line[128];
    uint64_t loops = 0, last_print_tick = 0;
    const long loop_ms = 20;
    long deadline_loops = run_seconds > 0 ? run_seconds * 1000 / loop_ms : 0;

    while (!s_quit && (deadline_loops == 0 || (long)loops < deadline_loops)) {
        HAL_Delay((uint32_t)loop_ms);
        loops++;

        while (app_io_tele_pop(&t)) tele = t;      /* 取最新快照 */
        eng_log_rec_t lr;                           /* log ring 排水（§5.3） */
        while (eng_log_pop(&lr))
            fprintf(stderr, "[log %c t=%.3fs] %s a=%ld b=%ld\n",
                    "IWE"[lr.level > 2 ? 2 : lr.level],
                    (double)lr.t_us / 1e6, eng_log_code_str(lr.code),
                    (long)lr.a, (long)lr.b);

        trace_drain(tf);                            /* trace ring 排水（H3） */

        while (app_io_health_pop(&hrec))           /* 取最新 bus 健康（G6） */
            if (hrec.bus < CO_BUS_COUNT) {
                hl[hrec.bus] = hrec;
                hn_feed_health(&hn, hrec.bus, &hrec.h);   /* §5.2 link 條款 */
            }

        if (loops % 5 == 0) hn_supervise(&hn);     /* 10 Hz 監督 */

        if (tele.tick >= last_print_tick + (uint64_t)rate_hz) {   /* ~1 Hz 狀態 */
            last_print_tick = tele.tick;
            printf("[app] t=%llus hn=%s sys=%s J0 sw=0x%04X pos=%ld tgt=%ld "
                   "drops=%lu late_max=%uus miss=%llu | L load=%u%% emcy=%lu "
                   "R load=%u%% emcy=%lu\n",
                   (unsigned long long)(tele.tick / (uint64_t)rate_hz),
                   hn_state_str(&hn), app_sys_state(), tele.sw0,
                   (long)tele.pos0, (long)tele.tgt0,
                   (unsigned long)tele.tx_drops, (unsigned)tele.late_max_us,
                   (unsigned long long)tele.miss,
                   hl[CO_BUS_LEFT].h.load_pct,
                   (unsigned long)hl[CO_BUS_LEFT].h.err_events,
                   hl[CO_BUS_RIGHT].h.load_pct,
                   (unsigned long)hl[CO_BUS_RIGHT].h.err_events);
            fflush(stdout);
        }

        if (poll_stdin(line, sizeof(line))) handle_cmd(line, &tele);
    }

    /* SHUTDOWN */
    s_rt_stop = 1;
    pthread_join(rt, NULL);
    hn_shutdown(&hn);
    if (tf) {
        trace_drain(tf);                            /* RT 已停,收尾排空 */
        fclose(tf);
        printf("trace → %s（drops=%lu）;分析：python3 tools/trace_report.py %s\n",
               trace_path, (unsigned long)eng_trace_drops(), trace_path);
    }
    printf("\n結束：ticks=%llu skipped=%llu miss=%lu overruns=%lu "
           "late_max=%uus drops=%lu tele_drops=%lu log_drops=%lu hn=%s sys=%s\n",
           (unsigned long long)eng_stats(&eng)->ticks,
           (unsigned long long)eng_stats(&eng)->skipped,
           (unsigned long)eng_stats(&eng)->miss,
           (unsigned long)eng_stats(&eng)->overruns,
           (unsigned)eng_stats(&eng)->late_max_us,
           (unsigned long)dual_arm_tx_drops(),
           (unsigned long)app_io_tele_drops(),
           (unsigned long)eng_log_drops(),
           hn_state_str(&hn), app_sys_state());
    return 0;
}
