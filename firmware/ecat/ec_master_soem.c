/**
 * @file    ec_master_soem.c
 * @brief   EtherCAT 主站門面的 SOEM（2.x context API）實作——方案 A（WP-L3）
 *
 * 由 tools/ecat_bringup/{phu_state2,phu_jog}.c 重構,配方常數取自 ec_config.h。
 * 與 sim / igh 後端擇一連結。只在有 SOEM（soem/soem.h + libsoem）處編譯：
 *   gcc ... -I$SOEM/include -I$SOEM/build/include -I$SOEM/osal ... -lsoem
 *
 * ⚠️ 狀態：配方（不重映射、DC assign_activate=0x300、SYNC0=週期、mode 走
 * PDO）與 IgH 後端一致,但 **DC 動作實測是在 IgH 上完成的**（2026-07-08,
 * gx701）;SOEM 端到 SAFEOP 已驗,完整 CSP+DC 轉動待於 gx701 覆核。
 *
 * DC 相位鎖定：本後端只做 ecx_configdc + dcsync0;把主站發幀時刻對齊
 * SYNC0 柵格的 PI 微調交給 loop engine + ec_dc_pll（eng_phase_trim_us,
 * WP-L2.2）,與既有架構一致。
 */
#include "ec_master.h"
#include "ec_config.h"
#include <stdlib.h>
#include <string.h>
#include "soem/soem.h"

static ecx_contextt s_ctx;
static char         s_iomap[512];
static int          s_naxes;
static int          s_last_wkc;

/** @brief 設定 EtherCAT 網卡名（pc_master 呼叫;未設則讀環境變數 EC_IFNAME）。 */
static const char *s_ifname;
void ec_soem_set_ifname(const char *n) { s_ifname = n; }

int ec_master_init(int expected_axes)
{
    const char *ifn = s_ifname ? s_ifname : getenv("EC_IFNAME");
    if (!ifn) return -1;
    if (!ecx_init(&s_ctx, ifn)) return -1;
    if (ecx_config_init(&s_ctx) <= 0) { ecx_close(&s_ctx); return -1; }
    /* 不重映射 PDO（此 drive 必須）——config_map 用出廠 0x1600/0x1A00 佈局 */
    ecx_config_map_group(&s_ctx, s_iomap, 0);
    ecx_configdc(&s_ctx);                          /* DC：CSP 必要 */
    s_naxes = s_ctx.slavecount > expected_axes ? expected_axes : s_ctx.slavecount;
    return s_naxes;
}

int ec_master_op(void)
{
    /* SYNC0 啟用（每軸）：CSP 動作的必要條件 */
    for (int a = 1; a <= s_naxes; a++)
        ecx_dcsync0(&s_ctx, (uint16_t)a, TRUE, EC_DC_SYNC0_NS, EC_DC_SYNC0_SHIFT_NS);

    ecx_statecheck(&s_ctx, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);
    (void)ec_master_exchange();
    s_ctx.slavelist[0].state = EC_STATE_OPERATIONAL;
    ecx_writestate(&s_ctx, 0);
    for (int i = 0; i < 300; i++) {
        (void)ec_master_exchange();
        ecx_statecheck(&s_ctx, 0, EC_STATE_OPERATIONAL, 20000);
        if ((s_ctx.slavelist[0].state & 0x0F) == EC_STATE_OPERATIONAL) return 0;
    }
    return -1;
}

int ec_master_exchange(void)
{
    ecx_send_processdata(&s_ctx);
    s_last_wkc = ecx_receive_processdata(&s_ctx, EC_TIMEOUTRET);
    return s_last_wkc;
}

int ec_master_expected_wkc(void)
{
    return s_ctx.grouplist[0].outputsWKC * 2 + s_ctx.grouplist[0].inputsWKC;
}

void ec_axis_set_output(int axis, const ec_out_t *o)
{
    if (axis < 0 || axis >= s_naxes) return;
    uint8_t *rx = s_ctx.slavelist[axis + 1].outputs;   /* SOEM slave 由 1 起算 */
    if (!rx) return;
    /* 出廠預設 RxPDO 佈局（EtherCAT little-endian = x86,直接寫） */
    rx[EC_RX_OFF_CW]     = (uint8_t)(o->controlword & 0xFF);
    rx[EC_RX_OFF_CW + 1] = (uint8_t)(o->controlword >> 8);
    rx[EC_RX_OFF_MODE]   = (uint8_t)EC_MODE_CSP;        /* mode 走 PDO */
    memcpy(rx + EC_RX_OFF_TARGET, &o->target_pos, 4);
}

void ec_axis_get_input(int axis, ec_in_t *i)
{
    if (axis < 0 || axis >= s_naxes) { i->statusword = 0; i->pos_actual = 0; return; }
    uint8_t *tx = s_ctx.slavelist[axis + 1].inputs;
    if (!tx) { i->statusword = 0; i->pos_actual = 0; return; }
    i->statusword = (uint16_t)(tx[EC_TX_OFF_SW] | (tx[EC_TX_OFF_SW + 1] << 8));
    memcpy(&i->pos_actual, tx + EC_TX_OFF_POS, 4);
}

int ec_axis_fresh(int axis) { (void)axis; return s_last_wkc >= ec_master_expected_wkc(); }

int ec_coe_read(int axis, uint16_t idx, uint8_t sub, uint32_t *val)
{
    int sz = 4;
    return ecx_SDOread(&s_ctx, (uint16_t)(axis + 1), idx, sub, FALSE, &sz, val,
                       EC_TIMEOUTRXM) > 0 ? 0 : -1;
}
int ec_coe_write(int axis, uint16_t idx, uint8_t sub, uint32_t val)
{
    return ecx_SDOwrite(&s_ctx, (uint16_t)(axis + 1), idx, sub, FALSE, 4, &val,
                        EC_TIMEOUTRXM) > 0 ? 0 : -1;
}

void ec_master_health(bus_health_t *h)
{
    memset(h, 0, sizeof(*h));
    h->proto[0] = (uint16_t)s_last_wkc;
    h->proto[1] = (uint16_t)ec_master_expected_wkc();
    h->sync_ok  = (s_last_wkc >= ec_master_expected_wkc());
}

void ec_master_close(void)
{
    if (s_naxes) {
        s_ctx.slavelist[0].state = EC_STATE_INIT;
        ecx_writestate(&s_ctx, 0);
        ecx_close(&s_ctx);
    }
    s_naxes = 0;
}
