"""
test_od.py — 假馬達物件字典 + 上位機 SDO 讀寫 單元測試
執行： python3 test_od.py   （回傳碼 0=通過）
"""
import sys
from host_console import Host
from phu_od import OD, od_access

fails = 0
def check(cond, msg):
    global fails
    if not cond:
        fails += 1
        print("  [FAIL]", msg)

def main():
    h = Host(model="PHU17", node=1)
    m = h.motor

    # OD 規模與存取（v1.06 手冊抽出的精選表,約 45 條）
    check(len(OD) >= 40, "OD 條目應 >= 40,實得 %d" % len(OD))
    check(od_access(0x6041) == "RO", "0x6041 statusword 應為 RO")

    # 讀身分/組態
    check(h.read(0x1000) == 0x00020192, "device type")
    check(h.read(0x26A0) == 1, "node id")
    check(h.read(0x26A1) == 1000000, "baudrate 1Mbps")
    check(h.read(0x6076) == 32000, "PHU17 rated torque 32 N·m → 32000 mNm")

    # 寫 RW 參數 → 讀回一致
    check(h.write(0x6060, 10) is True, "write mode RW ok")
    check(h.read(0x6061) == 10, "mode display 讀回 = 10")
    check(h.write(0x6065, 4321) is True, "write following-error-window ok")
    check(h.read(0x6065) == 4321, "讀回 4321")

    # 寫 RO → abort
    check(h.write(0x6041, 0x1234) is False, "寫 statusword(RO) 應 abort")
    check(h.write(0x6064, 999) is False, "寫 actual position(RO) 應 abort")
    check(h.read(0x6064) != 999, "RO 值不應被改寫")

    # 使能 + 移動 → 即時力/電流/位置
    h.write(0x6060, 8)            # CSP
    for cw in (0x80, 0x06, 0x07, 0x0F):
        h.pdo(cw, 0)
    check(m.enabled, "使能後 enabled")
    # 遮罩比較：新模型含動態位元（bit10 target-reached / bit12 setpoint-ack）
    check((h.read(0x6041) & 0x006F) == 0x0027, "statusword OP_ENABLED")
    for _ in range(300):
        h.pdo(0x0F, 150000)
    check(h.read(0x6064) > 100000, "actual position 已朝目標移動")
    check(abs(m.torque) > 0.1, "有輸出扭矩 (%.2f N·m)" % m.torque)
    check(abs(m.current) > 0.0, "有電流 (%.2f A)" % m.current)
    check(h.read(0x6077) != 0, "0x6077 實際扭矩‰ 非 0")
    check(h.read(0x6078) != 0, "0x6078 實際電流‰ 非 0")

    print("----------------------------------------")
    print("結果：%s（fails=%d）" % ("PASS" if fails == 0 else "FAIL", fails))
    return 0 if fails == 0 else 1

if __name__ == "__main__":
    # 靜音 host 的逐行列印,只看測試結果
    import io, contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = main()
    # 只印測試框架輸出（FAIL 行與結果）
    for ln in buf.getvalue().splitlines():
        if "[FAIL]" in ln or "結果" in ln or "---" in ln:
            print(ln)
    sys.exit(rc)
