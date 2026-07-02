"""
phu_od.py — EYOU PHU 物件字典（Object Dictionary）資料表

來源：`doc_EYOU/EYou-PHU&RHU系列关节CANopen与EtherCAT通信手册v1.06`，以 pdftotext -raw
抽出各運動模式章節的物件定義（index / sub / 名稱 / 單位 / 型別 / 存取 / PDO 對齊乾淨）。
手冊抽取不到型別的少數標準物件（torque/current/homing actual 值），以標準 CiA402 補齊並標 [std]。

此表是模型的「真相來源」：phu_motor 依此決定每個物件的型別寬度（編碼 SDO）、可讀可寫、預設值。
數值欄位的「即時值」（位置/扭矩/電流/狀態字…）由模型動態計算，不存在此表。
"""

# 型別 → 位元組數（SDO expedited 編碼用）；INT/UINT 預設 16-bit，加寬者另列
TYPE_SIZE = {
    "INT8": 1, "UINT8": 1, "INT": 2, "UINT": 2,
    "INT16": 2, "UINT16": 2, "INT32": 4, "UINT32": 4,
    "DINT": 4, "UDINT": 4, "STRING": 4,
}

# 存取：RW 可讀寫、RO 唯讀、WO 唯寫、CONST 常數
# pdo：RxPDO（主→從可映射）/ TxPDO（從→主可映射）/ NO（不可映射）
# 欄位：(name, type, access, unit, pdo, default)
OD = {
    # ---- 通訊 / 識別（CiA 301）----
    (0x1000, 0x00): ("Device type", "UDINT", "RO", "-", "NO", 0x00020192),
    (0x1008, 0x00): ("Device name", "STRING", "CONST", "-", "NO", 0),
    (0x1018, 0x01): ("Identity: Vendor ID", "UDINT", "RO", "-", "NO", 0x000000DE),
    (0x100A, 0x00): ("SW version", "STRING", "CONST", "-", "NO", 0),

    # ---- CiA 402 控制 / 狀態 ----
    (0x603F, 0x00): ("Error code", "UINT", "RO", "-", "TxPDO", 0),
    (0x6040, 0x00): ("Control word", "UINT", "RW", "-", "RxPDO", 0),
    (0x6041, 0x00): ("Status word", "UINT", "RO", "-", "TxPDO", 0x0040),
    (0x6060, 0x00): ("Modes of operation", "INT8", "RW", "-", "RxPDO", 8),
    (0x6061, 0x00): ("Modes of operation display", "INT8", "RO", "-", "TxPDO", 8),
    (0x6502, 0x00): ("Supported drive modes", "UDINT", "RO", "-", "NO", 0x000003ED),

    # ---- 位置 ----
    (0x6062, 0x00): ("Position demand value", "INT32", "RO", "plus", "TxPDO", 0),
    (0x6064, 0x00): ("Position actual value", "INT32", "RO", "plus", "TxPDO", 0),
    (0x6065, 0x00): ("Following error window", "UINT32", "RW", "plus", "NO", 0),
    (0x6067, 0x00): ("Position window", "UINT32", "RW", "plus", "NO", 0),
    (0x6068, 0x00): ("Position window time", "UINT", "RW", "0.05ms", "NO", 0),
    (0x607A, 0x00): ("Target position", "INT32", "RW", "plus", "RxPDO", 0),
    (0x607D, 0x01): ("Software position limit: min", "INT32", "RW", "plus", "NO", -2**31),
    (0x607D, 0x02): ("Software position limit: max", "INT32", "RW", "plus", "NO", 2**31 - 1),
    (0x60F4, 0x00): ("Following error actual value", "INT32", "RO", "plus", "TxPDO", 0),

    # ---- 速度 ----
    (0x606C, 0x00): ("Velocity actual value", "INT32", "RO", "plus/s", "TxPDO", 0),
    (0x6081, 0x00): ("Profile velocity", "UINT32", "RW", "plus/s", "TxPDO", 0),
    (0x607F, 0x00): ("Max profile velocity", "UINT32", "RW", "plus/s", "RxPDO", 0),
    (0x6083, 0x00): ("Profile acceleration", "UINT32", "RW", "plus/s^2", "TxPDO", 100000),
    (0x6084, 0x00): ("Profile deceleration", "UINT32", "RW", "plus/s^2", "TxPDO", 100000),
    (0x60FF, 0x00): ("Target velocity", "INT32", "RW", "plus/s", "RxPDO", 0),

    # ---- 力矩 / 電流（‰ 額定，[std] 為標準 CiA402 補）----
    (0x6071, 0x00): ("Target torque", "INT", "RW", "per-mille", "RxPDO", 0),
    (0x6072, 0x00): ("Max torque", "UINT", "RW", "per-mille", "RxPDO", 1000),
    (0x6074, 0x00): ("Torque demand", "INT", "RO", "per-mille", "TxPDO", 0),       # [std]
    (0x6075, 0x00): ("Motor rated current", "UINT32", "RW", "mA", "NO", 0),
    (0x6076, 0x00): ("Motor rated torque", "UINT32", "RW", "mNm", "NO", 0),
    (0x6077, 0x00): ("Torque actual value", "INT", "RO", "per-mille", "TxPDO", 0),  # [std]
    (0x6078, 0x00): ("Current actual value", "INT", "RO", "per-mille", "TxPDO", 0),
    (0x6087, 0x00): ("Torque slope", "UINT32", "RW", "per-mille/s", "RxPDO", 0),    # [std]

    # ---- Homing（mode 6）----
    (0x6098, 0x00): ("Homing method", "INT8", "RW", "-", "RxPDO", 35),              # [std]
    (0x6099, 0x01): ("Homing speeds: switch search", "UINT32", "RW", "plus/s", "NO", 0),  # [std]
    (0x6099, 0x02): ("Homing speeds: zero search", "UINT32", "RW", "plus/s", "NO", 0),    # [std]
    (0x609A, 0x00): ("Homing acceleration", "UINT32", "RW", "plus/s^2", "NO", 100000),    # [std]

    # ---- EYOU 製造商區（0x2xxx / 0x26Ax）----
    (0x2130, 0x00): ("Set mechanical zero", "UINT", "RW", "-", "NO", 0),
    (0x2260, 0x00): ("Homing error check mode", "UINT", "RW", "-", "NO", 0),
    (0x2262, 0x00): ("Homing acceleration (mfr)", "UDINT", "RW", "plus/s^2", "NO", 100000),
    (0x2265, 0x00): ("Homing offset (mfr)", "DINT", "RW", "plus", "NO", 0),
    (0x2266, 0x00): ("Homing max run time", "UINT32", "RW", "ms", "NO", 5000),
    (0x26A0, 0x00): ("Node ID", "UINT", "RW", "-", "NO", 1),
    (0x26A1, 0x00): ("Baudrate", "UDINT", "RW", "bps", "NO", 1000000),
}


def entry(index, sub=0):
    """取得 OD 條目 (name,type,access,unit,pdo,default)，找不到回 None。"""
    return OD.get((index, sub))


def size_of(index, sub=0):
    """物件位元組寬度（SDO 編碼用），未知預設 4。"""
    e = OD.get((index, sub))
    return TYPE_SIZE.get(e[1], 4) if e else 4


def is_writable(index, sub=0):
    e = OD.get((index, sub))
    return bool(e) and e[2] in ("RW", "WO")


def is_signed(index, sub=0):
    e = OD.get((index, sub))
    return bool(e) and e[1] in ("INT8", "INT", "INT16", "INT32", "DINT")


# ---------------------------------------------------------------------------
# 相容層：舊版 phu_od（自動抽取 399 條 list 格式）的 API。
# host_console / ws_server / test_od 等既有程式沿用；資料同一來源（上方 OD dict）。
#   OD_LIST     : [(index, sub, access, dtype, default, name), ...]
#   OD_BY_INDEX : {(index, sub): (access, dtype, default, name)}
#   od_name / od_access
# ---------------------------------------------------------------------------
OD_LIST = [(i, s, e[2], e[1], e[5], e[0]) for (i, s), e in sorted(OD.items())]
OD_BY_INDEX = {(i, s): (e[2], e[1], e[5], e[0]) for (i, s), e in OD.items()}


def od_name(index, sub=0):
    e = OD.get((index, sub))
    return e[0] if e else "0x%04X:%02X" % (index, sub)


def od_access(index, sub=0):
    e = OD.get((index, sub))
    return e[2] if e else "RW"
