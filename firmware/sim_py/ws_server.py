"""
ws_server.py — 假硬體 WebSocket 伺服器（純標準庫,無第三方相依）

把 phu_motor.py 的 14 顆真馬達（含完整 OD）跑成後端,前端 UI 透過 WebSocket：
  - 接收命令：enable / estop / jog / mode / read / write（OD 讀寫）
  - 廣播遙測：每顆馬達 cw/sw/狀態/目標/實際/扭矩/電流 + 最近 CAN 幀 + COB-ID 表

host_ui_ws.html 與 can_monitor_ws.html 連同一個 server → 看到同一份後端數據。

啟動： python3 ws_server.py [port]   （預設 8765）
"""
import socket, threading, time, json, base64, hashlib, struct, sys, collections
import os, math, functools
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from phu_motor import PhuMotor, CPR
from phu_od import od_name

BASE = os.path.dirname(os.path.abspath(__file__))         # .../firmware/sim_py
FIRMWARE_DIR = os.path.dirname(BASE)                      # .../firmware（靜態 HTTP 根）
MODEL_DIR = os.path.join(BASE, "model")
CONFIG_PATH = os.path.join(MODEL_DIR, "robot_config.json")


def load_config():
    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}

MAP = [("L_J1",0,1,"PHU20"),("L_J2",0,2,"PHU20"),("L_J3",0,3,"PHU17"),("L_J4",0,4,"PHU17"),
       ("L_J5",0,5,"PHU14"),("L_J6",0,6,"PHU14"),("L_J7",0,7,"PHU14"),
       ("R_J1",1,1,"PHU20"),("R_J2",1,2,"PHU20"),("R_J3",1,3,"PHU17"),("R_J4",1,4,"PHU17"),
       ("R_J5",1,5,"PHU14"),("R_J6",1,6,"PHU14"),("R_J7",1,7,"PHU14")]
STATE = {0x40:"SOD",0x21:"READY",0x23:"SWITCHED",0x27:"OP_ENABLED",0x07:"QSTOP",0x08:"FAULT"}
DT = 0.002

def sw_state(sw):
    """CiA402 狀態字 → 狀態名（位元遮罩;真主站的 sw 帶 bit10/12 等旗標,不能整值查表）。"""
    if (sw & 0x4F) == 0x40: return "SOD"
    if (sw & 0x6F) == 0x21: return "READY"
    if (sw & 0x6F) == 0x23: return "SWITCHED"
    if (sw & 0x6F) == 0x27: return "OP_ENABLED"
    if (sw & 0x6F) == 0x07: return "QSTOP"
    if (sw & 0x4F) == 0x08: return "FAULT"
    return "?"

class Sim:
    def __init__(self):
        self.M = [PhuMotor(n, m, nm) for (nm,b,n,m) in MAP]
        self.bus = [b for (nm,b,n,m) in MAP]
        self.goal = [0.0]*14          # 初始姿態 = 手臂垂下（全零，見設計文件零點慣例）
        self.cmd = [0.0]*14
        self.estop = False
        self.enseq = 0
        self.source = "sim"
        self.tx = [0,0]; self.rx = [0,0]
        self.cobid = {}                       # cobid -> dict
        self.frames = collections.deque(maxlen=30)
        self.lock = threading.Lock()
        self.config = load_config()
        self.model_rev = int(self.config.get("meta", {}).get("rev", 1))
        self.home = [0.0] * 14
        self._apply_config()

    def _apply_config(self):
        """把 config 的 control/home_offset 套用到 14 顆馬達（在鎖內呼叫）。"""
        ctrl = self.config.get("control", {})
        kp = ctrl.get("Kp"); kdr = ctrl.get("Kd_ratio")
        for m in self.M:
            if kp:
                m.Kp = float(kp)
                if kdr is not None:
                    m.Kd = float(kdr) * math.sqrt(m.Kp * m.I)
        joints = list(self.config.get("joints", {}).values())
        for i in range(len(self.M)):
            self.home[i] = float(joints[i].get("home_offset", 0.0)) if i < len(joints) else 0.0

    def set_config(self, msg):
        """整包 config 或 dotted-path 設定；持久化、bump rev、重新套用（在鎖內呼叫）。"""
        newcfg = msg.get("config")
        if newcfg is not None:
            self.config = newcfg
        elif msg.get("path") is not None:
            node = self.config
            keys = msg["path"].split(".")
            for k in keys[:-1]:
                node = node.setdefault(k, {})
            node[keys[-1]] = msg.get("value")
        self.model_rev += 1
        self.config.setdefault("meta", {})["rev"] = self.model_rev
        self._apply_config()
        try:
            with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                json.dump(self.config, f, ensure_ascii=False, indent=2)
        except Exception:
            pass

    def apply_preset(self, name):
        """套用具名姿態 preset 到各軸目標（在鎖內呼叫）。"""
        pre = self.config.get("presets", {}).get(name, {})
        for i, jn in enumerate(self.config.get("joints", {}).keys()):
            if jn in pre and i < len(self.goal):
                self.goal[i] = float(pre[jn])

    def _emit(self, direction, bus, cobid, kind, node, data, decode):
        if direction == "TX": self.tx[bus]+=1
        else: self.rx[bus]+=1
        self.cobid[cobid] = dict(dir=direction, kind=kind, node=node, dlc=len(data),
                                 data=" ".join("%02X"%x for x in data), decode=decode,
                                 count=self.cobid.get(cobid,{}).get("count",0)+1, t=time.time())
        self.frames.appendleft(dict(dir=direction, cobid="0x%03X"%cobid, kind=kind,
                                    data=" ".join("%02X"%x for x in data), decode=decode))

    def step(self, dt):
        with self.lock:
            for i,m in enumerate(self.M):
                b = self.bus[i]
                d = max(-dt*1.5, min(dt*1.5, self.goal[i]-self.cmd[i])); self.cmd[i]+=d
                if self.estop: cw=0x00
                elif not m.enabled: cw=[0x06,0x07,0x0F][min(2,self.enseq)]
                else: cw=0x0F
                target = int(round(self.cmd[i]*CPR)) if m.enabled else int(round(m.q*CPR))
                rp=[cw&0xff,cw>>8,target&0xff,(target>>8)&0xff,(target>>16)&0xff,(target>>24)&0xff]
                self._emit("TX",b,0x200+m.node_id,"RPDO1",m.node_id,rp,"cw=0x%02X pos=%d"%(cw,target))
                m.apply_controlword(cw); m.target_counts=target; m.step(dt)
                ap=int(round(m.q*CPR))
                tp=[m.statusword&0xff,m.statusword>>8,ap&0xff,(ap>>8)&0xff,(ap>>16)&0xff,(ap>>24)&0xff]
                self._emit("RX",b,0x180+m.node_id,"TPDO1",m.node_id,tp,
                           "sw=0x%04X(%s) pos=%d"%(m.statusword,STATE.get(m.statusword,"?"),ap))
            self.enseq+=1

    def command(self, msg):
        with self.lock:
            c = msg.get("cmd")
            if c=="enable": self.estop=False; self.enseq=0
            elif c=="estop": self.estop=True
            elif c=="mode":
                for m in self.M: m.write_od(0x6060,0,int(msg["value"]))
            elif c=="jog":
                j=int(msg["joint"]); self.goal[j]=float(msg["value"])
            elif c=="move":
                j=int(msg["joint"]); self.goal[j]+=float(msg.get("delta",0.3))
            elif c=="set_config": self.set_config(msg)
            elif c=="preset": self.apply_preset(msg.get("name"))
            elif c=="read":
                m=self.M[int(msg.get("node",1))-1 + (7 if msg.get("bus",0) else 0)]
                idx=int(msg["index"]); v=m.read_od(idx,int(msg.get("sub",0)))
                self._emit("TX",self.bus[self.M.index(m)],0x600+m.node_id,"SDO-req",m.node_id,
                           [0x40,idx&0xff,idx>>8,0,0,0,0,0],"read 0x%04X"%idx)
                self._emit("RX",self.bus[self.M.index(m)],0x580+m.node_id,"SDO-rsp",m.node_id,
                           [0x43,idx&0xff,idx>>8,0,v&0xff,(v>>8)&0xff,(v>>16)&0xff,(v>>24)&0xff],
                           "%s = %d"%(od_name(idx),v))
            elif c=="write":
                m=self.M[int(msg.get("node",1))-1 + (7 if msg.get("bus",0) else 0)]
                idx=int(msg["index"]); val=int(msg["value"]); ok=m.write_od(idx,int(msg.get("sub",0)),val)
                self._emit("TX",self.bus[self.M.index(m)],0x600+m.node_id,"SDO-req",m.node_id,
                           [0x2F,idx&0xff,idx>>8,0,val&0xff,0,0,0],"write 0x%04X=%d"%(idx,val))
                self._emit("RX",self.bus[self.M.index(m)],0x580+m.node_id,"SDO-rsp",m.node_id,
                           [0x60 if ok else 0x80,idx&0xff,idx>>8,0,0,0,0,0],
                           "OK" if ok else "ABORT(RO)")

    def telemetry(self):
        with self.lock:
            motors=[]
            for i,m in enumerate(self.M):
                motors.append(dict(name=m.name,model=m.model,bus=self.bus[i],node=m.node_id,
                    cw="0x%02X"%m.controlword, sw="0x%04X"%m.statusword,
                    state=sw_state(m.statusword), target=m.target_counts,
                    actual=int(round(m.q*CPR)), torque=round(m.torque,2), current=round(m.current,2),
                    peak=m.peak_torque, ratedC=m.rated_current,
                    q=round(m.q,5), qTarget=round(m.target_counts/CPR,5), home=round(self.home[i],5)))
            cobids=[dict(id="0x%03X"%k, **{kk:vv for kk,vv in v.items() if kk!="t"},
                         age=int((time.time()-v["t"])*1000)) for k,v in sorted(self.cobid.items())]
            return dict(motors=motors, frames=list(self.frames), cobids=cobids,
                        tx=self.tx, rx=self.rx, estop=self.estop, model_rev=self.model_rev,
                        source=getattr(self, "source", "sim"))

sim = Sim()
MODE = "sim"          # "sim"=純軟體自驅；"can"=真實 CAN 假從站；"monitor"=被動監聽鏡射

# ---- 控制迴圈執行緒（~500Hz）：sim 自驅;can 只推進物理;monitor 不動（位置來自 bus）----
def control_loop():
    last = time.time()
    while True:
        now = time.time()
        dt = min(now - last, 0.05)
        last = now
        if MODE == "sim":
            for _ in range(5): sim.step(DT)    # 5×2ms
        elif MODE == "can":
            # 假從站模式：主站的 RPDO/SDO 只設定目標,物理得自己推進
            #（否則 CSP 目標下去 q 永遠不動,3D 沒有動畫）
            with sim.lock:
                for m in sim.M: m.step(dt)
        time.sleep(0.01)
threading.Thread(target=control_loop, daemon=True).start()


# ===== 真實 CAN 模式：PC 當 14/7 顆假 CiA402 從站，回應 F746 主站 =====
def parse_nodes(spec):
    """'1:PHU20,2:PHU20,...' -> [(node, model), ...]"""
    out = []
    for item in spec.split(","):
        item = item.strip()
        if not item:
            continue
        nid, _, model = item.partition(":")
        out.append((int(nid), model or "PHU20"))
    return out

def _cob_kind(cobid):
    if cobid == 0x000: return "NMT"
    if 0x180 <= cobid <= 0x1FF: return "TPDO1"
    if 0x200 <= cobid <= 0x27F: return "RPDO1"
    if 0x580 <= cobid <= 0x5FF: return "SDO-rsp"
    if 0x600 <= cobid <= 0x67F: return "SDO-req"
    if 0x700 <= cobid <= 0x77F: return "HB"
    return "?"

def can_loop(interface, channel, bitrate, nodes):
    """開 python-can bus，用 can_slave.CiA402Slave 回應主站；馬達 q 反映真實 CANopen 指令。"""
    global MODE
    try:
        import can
    except ImportError:
        print("需要 python-can：pip install python-can pyserial（見 run_wsl.sh / run_ubuntu.sh）",
              file=sys.stderr)
        return
    from can_slave import CiA402Slave, HEARTBEAT, NS_BOOTUP

    # 每個要模擬的 node 綁到 sim 的左臂馬達（index=node-1），使 3D/遙測與 CAN 同一份狀態
    slaves = []
    for node, model in nodes:
        s = CiA402Slave(node, model, "L_J%d" % node)
        idx = node - 1
        if 0 <= idx < len(sim.M):
            s.motor = sim.M[idx]          # 共用同一顆 PhuMotor → 遙測/3D 立即反映
        slaves.append(s)

    try:
        bus = can.Bus(interface=interface, channel=channel, bitrate=bitrate)
    except Exception as e:
        print("開啟 CAN 失敗（interface=%s channel=%s）：%s" % (interface, channel, e), file=sys.stderr)
        return
    sim.source = "canable:%s@%s" % (interface, channel)
    print("CAN 模式：以 node %s 當假從站回應主站；等待 F746 …"
          % ",".join(str(n) for n, _ in nodes))

    for s in slaves:                      # boot-up heartbeat
        bus.send(can.Message(arbitration_id=HEARTBEAT + s.node_id,
                             data=[NS_BOOTUP], is_extended_id=False))
    while True:
        msg = bus.recv(timeout=0.2)
        if msg is None or msg.is_extended_id:
            continue
        arb, data = msg.arbitration_id, list(msg.data)
        with sim.lock:
            sim._emit("RX", 0, arb, _cob_kind(arb), 0, data, "")
            for s in slaves:
                for a, d in s.handle_frame(arb, data):
                    bus.send(can.Message(arbitration_id=a, data=bytes(d), is_extended_id=False))
                    sim._emit("TX", 0, a, _cob_kind(a), s.node_id, list(d), "")

# ===== 被動監聽模式：嗅探真實 bus,把主站⇄從站的交握鏡射到 3D/資料流 =====
# 情境：pc_master 或 F746 當主站,can_slave.py（或真 EYOU 馬達）當從站,
# 本程式第三方旁聽——TPDO 回授餵 3D 動畫,所有幀餵資料流面板。
# 接真馬達時同樣適用（P5 文件的情境③：被動讀 0x6064 真編碼器）。
_mon_gate = {}   # (bus,cobid) -> 上次完整記錄時間;取樣 20Hz,其餘只累計數

def _monitor_sniff(b, bus):
    while True:
        msg = bus.recv(timeout=0.2)
        if msg is None or msg.is_extended_id:
            continue
        arb, data = msg.arbitration_id, list(msg.data)
        kind = _cob_kind(arb)
        node = arb & 0x7F
        with sim.lock:
            m = sim.M[b*7 + node - 1] if 1 <= node <= 7 else None
            decode = ""
            if kind == "TPDO1" and m and len(data) >= 6:
                sw = data[0] | (data[1] << 8)
                pos = int.from_bytes(bytes(data[2:6]), "little", signed=True)
                m.statusword = sw
                m.q = pos / CPR                      # ← 3D 動畫的資料源
                decode = "sw=0x%04X(%s) pos=%d" % (sw, sw_state(sw), pos)
            elif kind == "RPDO1" and m and len(data) >= 6:
                cw = data[0] | (data[1] << 8)
                tgt = int.from_bytes(bytes(data[2:6]), "little", signed=True)
                m.controlword = cw
                m.target_counts = tgt
                decode = "cw=0x%02X pos=%d" % (cw, tgt)
            dirn = "TX" if kind in ("RPDO1", "SDO-req", "NMT") else "RX"
            now = time.time()
            key = (b, arb)
            if now - _mon_gate.get(key, 0) >= 0.05:  # 500Hz×28 幀全記錄太貴,取樣即可
                _mon_gate[key] = now
                sim._emit(dirn, b, arb, kind, node, data, decode)
            else:
                (sim.tx if dirn == "TX" else sim.rx)[b] += 1

def monitor_loop(interface, channels, bitrate):
    try:
        import can
    except ImportError:
        print("需要 python-can：pip install python-can", file=sys.stderr)
        return
    opened = []
    for b, ch in enumerate(channels):
        if not ch:
            continue
        try:
            opened.append((b, ch, can.Bus(interface=interface, channel=ch, bitrate=bitrate)))
        except Exception as e:
            print("開啟 CAN 失敗（%s@%s）：%s" % (interface, ch, e), file=sys.stderr)
    if not opened:
        return
    sim.source = "monitor:%s@%s" % (interface, "+".join(ch for _, ch, _ in opened))
    print("監聽模式：旁聽 %s（bus %s）,3D/資料流鏡射真實交握"
          % ("+".join(ch for _, ch, _ in opened), ",".join(str(b) for b, _, _ in opened)))
    for b, _, bus in opened:
        threading.Thread(target=_monitor_sniff, daemon=True, args=(b, bus)).start()


# ===== 極簡 WebSocket（RFC6455）=====
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
clients = set(); clients_lock = threading.Lock()

def ws_send(conn, text):
    data = text.encode("utf-8"); n=len(data)
    hdr = bytearray([0x81])
    if n<126: hdr.append(n)
    elif n<65536: hdr += bytes([126]) + struct.pack(">H", n)
    else: hdr += bytes([127]) + struct.pack(">Q", n)
    conn.sendall(hdr+data)

def recvn(conn, n):
    buf=b""
    while len(buf)<n:
        c=conn.recv(n-len(buf))
        if not c: return None
        buf+=c
    return buf

def ws_recv(conn):
    h=recvn(conn,2)
    if not h: return None
    op=h[0]&0x0f; masked=h[1]&0x80; ln=h[1]&0x7f
    if ln==126: ln=struct.unpack(">H",recvn(conn,2))[0]
    elif ln==127: ln=struct.unpack(">Q",recvn(conn,8))[0]
    mask=recvn(conn,4) if masked else b"\0\0\0\0"
    pl=bytearray(recvn(conn,ln) or b"")
    for i in range(len(pl)): pl[i]^=mask[i%4]
    if op==0x8: return None
    return pl.decode("utf-8","ignore")

def handle_client(conn):
    try:
        req=conn.recv(4096).decode("utf-8","ignore")
        key=None
        for line in req.split("\r\n"):
            if line.lower().startswith("sec-websocket-key:"):
                key=line.split(":",1)[1].strip()
        if not key: conn.close(); return
        accept=base64.b64encode(hashlib.sha1((key+GUID).encode()).digest()).decode()
        conn.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                      "Connection: Upgrade\r\nSec-WebSocket-Accept: "+accept+"\r\n\r\n").encode())
        with clients_lock: clients.add(conn)
        while True:
            msg=ws_recv(conn)
            if msg is None: break
            try: sim.command(json.loads(msg))
            except Exception: pass
    except Exception:
        pass
    finally:
        with clients_lock: clients.discard(conn)
        try: conn.close()
        except Exception: pass

def broadcaster():
    while True:
        time.sleep(0.05)               # 20 Hz 遙測
        payload=json.dumps(sim.telemetry())
        with clients_lock: cs=list(clients)
        for c in cs:
            try: ws_send(c, payload)
            except Exception:
                with clients_lock: clients.discard(c)
threading.Thread(target=broadcaster, daemon=True).start()

# ===== 靜態 HTTP 伺服器（serve firmware/：/ui/*.html 與 /sim_py/model/*）=====
class _Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()
    def log_message(self, *a):
        pass

def start_http(pref):
    """在 pref..pref+20 找一個可用埠啟動靜態 HTTP，回傳實際埠（找不到回 None）。"""
    handler = functools.partial(_Handler, directory=FIRMWARE_DIR)
    for p in range(pref, pref + 21):
        try:
            httpd = ThreadingHTTPServer(("0.0.0.0", p), handler)
        except OSError:
            continue
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
        return p
    return None

ONE_ARM = "1:PHU20,2:PHU20,3:PHU17,4:PHU17,5:PHU14,6:PHU14,7:PHU14"

def main():
    global MODE
    import argparse
    ap = argparse.ArgumentParser(description="PHU 假硬體：WebSocket + 靜態 HTTP + 3D，"
                                             "可選真實 CAN 模式（當假 CiA402 從站測 F746 主站）")
    ap.add_argument("ws_port", nargs="?", type=int, default=8765, help="WebSocket 埠（預設 8765）")
    ap.add_argument("http_port", nargs="?", type=int, default=8090, help="HTTP 偏好埠（預設 8090，占用自動避讓）")
    ap.add_argument("--interface", help="python-can interface（slcan/gs_usb…）；給了才進真實 CAN 模式")
    ap.add_argument("--channel", help="CAN 通道（slcan=COM11 或 /dev/ttyACM0；gs_usb=0；socketcan=vcan0）")
    ap.add_argument("--channel2", help="第二條 bus=右臂（monitor 模式；如 vcan1）")
    ap.add_argument("--bitrate", type=int, default=1000000, help="位元率（預設 1Mbps）")
    ap.add_argument("--nodes", default=ONE_ARM, help="真實 CAN 模式要模擬的 node（預設單臂 7 顆）")
    ap.add_argument("--monitor", action="store_true",
                    help="被動監聽：不當從站,旁聽 bus 鏡射到 3D/資料流"
                         "（主站+從站另跑,如 pc_master + can_slave.py）")
    args = ap.parse_args()

    http_port=start_http(args.http_port)
    s=socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", args.ws_port)); s.listen(8)

    if args.interface:
        if args.monitor:
            MODE = "monitor"
            threading.Thread(target=monitor_loop, daemon=True,
                             args=(args.interface, [args.channel, args.channel2], args.bitrate)).start()
        else:
            MODE = "can"
            threading.Thread(target=can_loop, daemon=True,
                             args=(args.interface, args.channel, args.bitrate, parse_nodes(args.nodes))).start()

    mode_str = {"sim": "純軟體 sim", "can": "真實 CAN 假從站", "monitor": "真實 CAN 監聽"}[MODE]
    print("假硬體伺服器啟動（模式：%s）：" % mode_str)
    print("  WebSocket : ws://localhost:%d"%args.ws_port)
    if http_port:
        print("  3D 檢視器 : http://localhost:%d/ui/viewer3d.html"%http_port)
        print("  模型/設定 : http://localhost:%d/sim_py/model/dual_arm.urdf"%http_port)
        if http_port != args.http_port:
            print("  (偏好埠 %d 被占用，自動改用 %d)"%(args.http_port, http_port))
    else:
        print("  [警告] HTTP 埠 %d..%d 皆被占用；請指定空埠：python3 ws_server.py 8765 <free-port>"%(args.http_port, args.http_port+20))
    if args.interface:
        if args.monitor:
            print("  CAN       : 監聽 %s @ %s%s" % (args.interface, args.channel,
                  ("+" + args.channel2) if args.channel2 else ""))
        else:
            print("  CAN       : %s @ %s（模擬 node %s）" % (args.interface, args.channel, args.nodes))
    print("  (Ctrl+C 結束)")
    while True:
        conn,_=s.accept()
        threading.Thread(target=handle_client, args=(conn,), daemon=True).start()

if __name__=="__main__":
    main()
