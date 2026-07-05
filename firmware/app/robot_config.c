/**
 * @file    robot_config.c
 * @brief   機器人參數設定（佔位值,待 WP0.4 實機填入）
 *
 * 項目 7：key=value 設定檔覆寫編譯期預設（鍵格式見 robot_config.h）。
 * 編譯期值仍是唯一的「預設」,檔案只做覆寫——沒有檔案行為完全不變;
 * WP0.4 實機量測值放檔案,免重編譯、逐機一份。
 */
#include "robot_config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979f
/* EYOU 輸出端 19-bit → 524288 counts/rev → counts/rad（實際以驅動 user-unit 為準） */
#define CPR_19BIT (524288.0f / (2.0f*PI_F))

/* 14 軸 joint_space 設定（左 0..6, 右 7..13）。限位/速度為保守佔位值。 */
static js_joint_cfg_t s_js[JS_TOTAL_JOINTS];

/* 7-DoF 擬人臂 DH（S-R-S 結構）佔位參數。單位：公尺 / 弧度。
 * TODO(WP0.4)：以實機機構（連桿長度、扭轉、偏移）取代。 */
static const arm_kin_t S_LEFT_DEFAULT = {{
    /*    a       alpha        d       theta_off */
    { 0.0f,  -PI_F/2,  0.10f,   0.0f },   /* J1 肩 */
    { 0.0f,   PI_F/2,  0.0f,    0.0f },   /* J2 肩 */
    { 0.0f,  -PI_F/2,  0.30f,   0.0f },   /* J3 肩 yaw（上臂） */
    { 0.0f,   PI_F/2,  0.0f,    0.0f },   /* J4 肘 */
    { 0.0f,  -PI_F/2,  0.28f,   0.0f },   /* J5 腕（前臂） */
    { 0.0f,   PI_F/2,  0.0f,    0.0f },   /* J6 腕 */
    { 0.0f,   0.0f,    0.08f,   0.0f },   /* J7 腕（末端） */
}, { 0.0f, 0.20f, 0.0f }};                /* 左肩基座：世界 +Y 0.20m */
static arm_kin_t s_left;
static arm_kin_t s_right; /* 右臂於 init 複製左臂（實機可鏡像） */

static const ik_cfg_t S_IK_DEFAULT = {
    .lambda = 0.05f,
    .step_gain = 0.5f,
    .max_iters = 100,
    .pos_tol = 0.001f,   /* 1 mm */
    .rot_tol = 0.01f,    /* ~0.57° */
};
static ik_cfg_t s_ik;

static const float S_Q_INIT_DEFAULT[ARM_DOF] =
    { 0.0f, 0.3f, 0.0f, 0.7f, 0.0f, 0.5f, 0.0f };
static float s_q_init[ARM_DOF];

static int s_inited = 0;
static void ensure_init(void)
{
    if (s_inited) return;
    s_left = S_LEFT_DEFAULT;
    s_ik   = S_IK_DEFAULT;
    memcpy(s_q_init, S_Q_INIT_DEFAULT, sizeof(s_q_init));
    s_right = s_left;                 /* 佔位：右臂同左臂 DH（實機請填鏡像 DH） */
    s_right.base_p[1] = -0.20f;       /* 右肩基座：世界 -Y 0.20m（與左臂分開 0.4m） */
    for (int j=0;j<JS_TOTAL_JOINTS;j++){
        s_js[j].counts_per_rad = CPR_19BIT;
        s_js[j].q_min = -PI_F;       /* 佔位限位 ±180° */
        s_js[j].q_max =  PI_F;
        s_js[j].vmax  =  2.0f;       /* rad/s 佔位 */
        s_js[j].amax  =  8.0f;       /* rad/s^2 佔位 */
        s_js[j].offset_rad = 0.0f;
    }
    s_inited = 1;
}

const js_joint_cfg_t *robot_js_cfg(void){ ensure_init(); return s_js; }
const arm_kin_t *robot_left_kin(void){ ensure_init(); return &s_left; }
const arm_kin_t *robot_right_kin(void){ ensure_init(); return &s_right; }
const ik_cfg_t *robot_ik_cfg(void){ ensure_init(); return &s_ik; }
const float *robot_q_init(void){ ensure_init(); return s_q_init; }

void robot_config_reset(void){ s_inited = 0; ensure_init(); }

/* ================= 設定檔（項目 7）================= */

/** @brief 取下一段 token（以 '.' 分界）;回長度、*next 移到下一段。 */
static int tok(const char **next, char *out, int cap)
{
    const char *p = *next;
    int n = 0;
    while (*p && *p != '.' && n < cap - 1) out[n++] = *p++;
    out[n] = '\0';
    *next = (*p == '.') ? p + 1 : p;
    return n;
}

/** @brief joint 欄位指標（依名稱）;未知回 NULL。 */
static float *js_field(js_joint_cfg_t *j, const char *f)
{
    if (!strcmp(f, "counts_per_rad")) return &j->counts_per_rad;
    if (!strcmp(f, "q_min"))          return &j->q_min;
    if (!strcmp(f, "q_max"))          return &j->q_max;
    if (!strcmp(f, "vmax"))           return &j->vmax;
    if (!strcmp(f, "amax"))           return &j->amax;
    if (!strcmp(f, "offset_rad"))     return &j->offset_rad;
    return NULL;
}

/** @brief 解析軸號 token;"*" 回 -2（萬用）,壞值回 -1。 */
static int parse_idx(const char *t, int max)
{
    if (!strcmp(t, "*")) return -2;
    char *end;
    long v = strtol(t, &end, 10);
    return (*end == '\0' && end != t && v >= 0 && v < max) ? (int)v : -1;
}

/** @brief 套用/驗證單鍵（dry=1 只驗證不寫入）。 */
static int apply_ex(const char *key, float value, int dry)
{
    ensure_init();
    char t[32];
    const char *p = key;
    if (!tok(&p, t, sizeof(t))) return -1;

    if (!strcmp(t, "joint")) {
        if (!tok(&p, t, sizeof(t))) return -1;
        int idx = parse_idx(t, JS_TOTAL_JOINTS);
        if (idx == -1) return -1;
        if (!tok(&p, t, sizeof(t)) || *p) return -1;
        if (!js_field(&s_js[0], t)) return -1;
        if (dry) return 0;
        int lo = (idx == -2) ? 0 : idx, hi = (idx == -2) ? JS_TOTAL_JOINTS - 1 : idx;
        for (int j = lo; j <= hi; j++) *js_field(&s_js[j], t) = value;
        return 0;
    }
    if (!strcmp(t, "ik")) {
        if (!tok(&p, t, sizeof(t)) || *p) return -1;
        float *f = !strcmp(t, "lambda")    ? &s_ik.lambda :
                   !strcmp(t, "step_gain") ? &s_ik.step_gain :
                   !strcmp(t, "pos_tol")   ? &s_ik.pos_tol :
                   !strcmp(t, "rot_tol")   ? &s_ik.rot_tol : NULL;
        if (f)                        { if (!dry) *f = value; return 0; }
        if (!strcmp(t, "max_iters"))  { if (!dry) s_ik.max_iters = (int)value; return 0; }
        return -1;
    }
    if (!strcmp(t, "q_init")) {
        if (!tok(&p, t, sizeof(t)) || *p) return -1;
        int idx = parse_idx(t, ARM_DOF);
        if (idx < 0) return -1;
        if (!dry) s_q_init[idx] = value;
        return 0;
    }
    if (!strcmp(t, "base")) {
        if (!tok(&p, t, sizeof(t))) return -1;
        arm_kin_t *arm = !strcmp(t, "left")  ? &s_left :
                         !strcmp(t, "right") ? &s_right : NULL;
        if (!arm || !tok(&p, t, sizeof(t)) || *p || t[1]) return -1;
        int ax = t[0] - 'x';
        if (ax < 0 || ax > 2) return -1;
        if (!dry) arm->base_p[ax] = value;
        return 0;
    }
    if (!strcmp(t, "dh")) {
        if (!tok(&p, t, sizeof(t))) return -1;
        arm_kin_t *arm = !strcmp(t, "left")  ? &s_left :
                         !strcmp(t, "right") ? &s_right : NULL;
        if (!arm || !tok(&p, t, sizeof(t))) return -1;
        int idx = parse_idx(t, ARM_DOF);
        if (idx < 0 || !tok(&p, t, sizeof(t)) || *p) return -1;
        dh_t *d = &arm->dh[idx];
        float *f = !strcmp(t, "a")     ? &d->a :
                   !strcmp(t, "alpha") ? &d->alpha :
                   !strcmp(t, "d")     ? &d->d :
                   !strcmp(t, "theta") ? &d->theta_off : NULL;
        if (!f) return -1;
        if (!dry) *f = value;
        return 0;
    }
    return -1;
}

int robot_config_apply(const char *key, float value)
{
    return apply_ex(key, value, 0);
}

/** @brief 解析一行;回 1=有效鍵值、0=空行/註解、-1=格式錯。 */
static int parse_line(char *line, char **key, float *value)
{
    char *p = line + strspn(line, " \t");
    p[strcspn(p, "\r\n")] = '\0';
    if (*p == '\0' || *p == '#') return 0;
    char *eq = strchr(p, '=');
    if (!eq) return -1;
    char *k_end = eq;
    while (k_end > p && (k_end[-1] == ' ' || k_end[-1] == '\t')) k_end--;
    *k_end = '\0';
    if (*p == '\0') return -1;
    char *end;
    *value = strtof(eq + 1, &end);
    if (end == eq + 1) return -1;
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\0' && *end != '#') return -1;
    *key = p;
    return 1;
}

int robot_config_load(const char *path)
{
    /* 兩趟：先全檔驗證,全過才套用——壞檔不會半套用 */
    for (int pass = 0; pass < 2; pass++) {
        FILE *f = fopen(path, "r");
        if (!f) return -1;
        char line[160], *key;
        float value;
        int applied = 0, lineno = 0;
        while (fgets(line, sizeof(line), f)) {
            lineno++;
            int rc = parse_line(line, &key, &value);
            if (rc == 0) continue;
            if (rc < 0 || apply_ex(key, value, pass == 0) != 0) {
                fclose(f);
                fprintf(stderr, "[config] %s:%d 壞鍵/格式：%s\n",
                        path, lineno, line);
                return -2;
            }
            applied++;
        }
        fclose(f);
        if (pass == 1) return applied;
    }
    return -2;                        /* 不可達 */
}
