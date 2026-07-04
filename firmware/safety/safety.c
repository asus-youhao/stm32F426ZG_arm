/**
 * @file    safety.c
 * @brief   WP6 安全與系統狀態機實作
 */
#include "safety.h"

/* CiA402 狀態字故障位 */
#define SW_FAULT_BIT   0x0008u
#define SW_OP_ENABLED  0x0027u
#define SW_OP_MASK     0x006Fu

#define CW_QUICK_STOP      0x0002u
#define CW_DISABLE_VOLTAGE 0x0000u

static safety_cfg_t s_cfg;
static bool     s_estop;
static sys_state_t s_state;
static uint16_t s_sw[SAFETY_JOINTS];
static uint32_t s_last_ms[SAFETY_JOINTS];
static bool     s_emcy[SAFETY_JOINTS];   /* WP-H4/G5：EMCY 未復歸的軸 */

void safety_init(const safety_cfg_t *cfg)
{
    s_cfg = *cfg;
    s_estop = false;
    s_state = SYS_INIT;
    for (int i=0;i<SAFETY_JOINTS;i++){ s_sw[i]=0; s_last_ms[i]=0; s_emcy[i]=false; }
}

void safety_set_estop(bool active){ s_estop = active; }

void safety_report_joint(int j, uint16_t sw, uint32_t now_ms)
{
    if (j<0 || j>=SAFETY_JOINTS) return;
    s_sw[j] = sw;
    s_last_ms[j] = now_ms;
}

void safety_report_emcy(int j, bool active)
{
    if (j<0 || j>=SAFETY_JOINTS) return;
    s_emcy[j] = active;   /* 非零故障碼鎖存;error reset(0x0000) 解除 */
}

bool safety_update(uint32_t now_ms)
{
    if (s_estop) { s_state = SYS_ESTOP; return false; }

    bool any_fault = false, any_stale = false;
    bool all_enabled = true, any_enabled = false;
    for (int j=0;j<SAFETY_JOINTS;j++){
        if (s_sw[j] & SW_FAULT_BIT) any_fault = true;
        if (s_emcy[j]) any_fault = true;   /* EMCY = safe stop 條款（G5） */
        /* last_ms==0 表示此軸尚未收過任何回授（啟動初期）→ 不視為失聯,
           待第一筆回授後才納入看門狗。已活過再失聯則會被偵測。 */
        if (s_last_ms[j] != 0 &&
            (now_ms - s_last_ms[j]) > s_cfg.comms_timeout_ms) any_stale = true;
        if ((s_sw[j] & SW_OP_MASK) == SW_OP_ENABLED) any_enabled = true;
        else all_enabled = false;
    }

    if (any_fault || any_stale) { s_state = SYS_FAULT; return false; }

    /* RUNNING 門檻由設定決定：require=true 需全軸 op-enabled,
       false 則任一軸即可（部分軸運轉,如單軸 bring-up/HIL）。
       未達門檻 → ENABLED（可繼續使能交握,但上層應鎖住運動目標）。 */
    bool run_ok = s_cfg.require_all_enabled_for_run ? all_enabled : any_enabled;
    s_state = run_ok ? SYS_RUNNING : SYS_ENABLED;

    /* 回傳只表示「無危險」（estop/fault/失聯之外）——不可因未達 RUNNING
       門檻回 false,否則 safe-stop 會覆寫控制字,使能交握永遠完成不了。 */
    return true;
}

sys_state_t safety_state(void){ return s_state; }

const char *safety_state_str(void)
{
    switch (s_state){
        case SYS_INIT: return "INIT";
        case SYS_IDLE: return "IDLE";
        case SYS_ENABLED: return "ENABLED";
        case SYS_RUNNING: return "RUNNING";
        case SYS_FAULT: return "FAULT";
        case SYS_ESTOP: return "ESTOP";
        default: return "?";
    }
}

uint16_t safety_safe_controlword(void)
{
    /* 急停用 disable voltage（自由）;一般故障用 quick stop（受控停） */
    return (s_state == SYS_ESTOP) ? CW_DISABLE_VOLTAGE : CW_QUICK_STOP;
}
