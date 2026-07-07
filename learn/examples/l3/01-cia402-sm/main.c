/**
 * L3-13 · CiA402 狀態機 — 純函式版（對照 learn/l3-adv.html 第 13 節）
 *
 * 與 firmware/canopen/cia402.c 同一套解碼/推步邏輯的獨立最小版，
 * main 走一遍「上電→使能→Fault→復歸」流程當自我測試。
 */
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

typedef enum { DS_NOT_READY, DS_SWITCH_ON_DISABLED, DS_READY_TO_SWITCH_ON,
               DS_SWITCHED_ON, DS_OPERATION_ENABLED, DS_QUICK_STOP,
               DS_FAULT, DS_UNKNOWN } state_t;

static const char *name[] = { "NOT_READY", "SWITCH_ON_DISABLED", "READY",
                              "SWITCHED_ON", "OPERATION_ENABLED", "QUICK_STOP",
                              "FAULT", "UNKNOWN" };

static state_t decode(uint16_t sw)          /* CiA402 statusword bit 遮罩 */
{
    if ((sw & 0x004F) == 0x0000) return DS_NOT_READY;
    if ((sw & 0x004F) == 0x0040) return DS_SWITCH_ON_DISABLED;
    if ((sw & 0x006F) == 0x0021) return DS_READY_TO_SWITCH_ON;
    if ((sw & 0x006F) == 0x0023) return DS_SWITCHED_ON;
    if ((sw & 0x006F) == 0x0027) return DS_OPERATION_ENABLED;
    if ((sw & 0x006F) == 0x0007) return DS_QUICK_STOP;
    if ((sw & 0x004F) == 0x0008) return DS_FAULT;
    return DS_UNKNOWN;
}

static uint16_t enable_step(uint16_t sw)    /* statusword 進、controlword 出 */
{
    switch (decode(sw)) {
    case DS_FAULT:              return 0x0080;  /* fault reset */
    case DS_SWITCH_ON_DISABLED: return 0x0006;  /* shutdown */
    case DS_READY_TO_SWITCH_ON: return 0x0007;  /* switch on */
    case DS_SWITCHED_ON:        return 0x000F;  /* enable operation */
    case DS_OPERATION_ENABLED:  return 0x000F;  /* 保持 */
    default:                    return 0x0006;
    }
}

int main(void)
{
    /* 模擬驅動器對每個 controlword 的 statusword 回應序列 */
    uint16_t sw = 0x0040;                        /* 上電: SWITCH_ON_DISABLED */
    printf("開始: sw=0x%04x (%s)\n", sw, name[decode(sw)]);
    const struct { uint16_t cw, next_sw; } drive[] = {
        { 0x0006, 0x0021 }, { 0x0007, 0x0023 }, { 0x000F, 0x0027 },
    };
    for (int i = 0; i < 3; i++) {
        uint16_t cw = enable_step(sw);
        assert(cw == drive[i].cw);               /* 推步必須照教科書順序 */
        sw = drive[i].next_sw;
        printf("send cw=0x%04x -> sw=0x%04x (%s)\n", cw, sw, name[decode(sw)]);
    }
    assert(decode(sw) == DS_OPERATION_ENABLED);

    sw = 0x0008;                                 /* 驅動器突然報 FAULT */
    printf("FAULT! sw=0x%04x -> 下一步 cw=0x%04x (reset)\n", sw, enable_step(sw));
    assert(enable_step(sw) == 0x0080);

    printf("全部斷言通過 — 對照 firmware/tests/test_cia402.c\n");
    return 0;
}
