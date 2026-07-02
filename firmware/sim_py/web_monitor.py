"""
web_monitor.py — C1 上位機 web 看板 + 控制台 + CANopen 資料流（純標準庫）

雙臂 14 軸即時遙測 + 從瀏覽器切 8 模式/下控制字/設目標/注入故障，並即時顯示**選取那顆
motor 的真實 CANopen rx/tx 幀流**（RPDO/TPDO/SDO/NMT/HB，依 COB-ID 解析）。

  python web_monitor.py --demo                      # http://127.0.0.1:8080
  python web_monitor.py --interface gs_usb --channel 0 --bitrate 1000000

資料流方向以「從站視角」：RX = 主站→從站（RPDO1 / SDO-req / NMT）；TX = 從站→主站
（TPDO1 / SDO-rsp / HB）。demo 無實體 bus，故對追蹤的那顆 motor 產生等效幀交換。
"""
import argparse
import collections
import json
import math
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from phu_motor import PhuMotor, CPR
from can_slave import CiA402Slave, SDO_RX, SDO_TX, RPDO1, TPDO1, HEARTBEAT, NMT

JOINTS = []
for arm in ("L", "R"):
    for node, model, part in [
        (1, "PHU20", "Shoulder"), (2, "PHU20", "Shoulder"), (3, "PHU17", "Shoulder-Yaw"),
        (4, "PHU17", "Elbow"), (5, "PHU14", "Wrist1"), (6, "PHU14", "Wrist2"),
        (7, "PHU14", "Wrist3"),
    ]:
        JOINTS.append(dict(arm=arm, node=node, model=model,
                           name="%s_J%d_%s" % (arm, node, part)))

SW_NAMES = {0x40: "switch-on-disabled", 0x21: "ready", 0x23: "switched-on",
            0x27: "operation-enabled", 0x07: "quick-stop", 0x0F: "fault-reaction",
            0x08: "fault", 0x00: "not-ready"}
MODE_NAMES = {1: "PP", 3: "PV", 4: "PT", 6: "HM", 8: "CSP", 9: "CSV", 10: "CST", 13: "CSF"}


def decode_sw(sw):
    return SW_NAMES.get(sw & 0x006F, "0x%04X" % (sw & 0xFFFF))


def frame_kind(cobid):
    if cobid == NMT: return "NMT"
    if 0x080 <= cobid <= 0x0FF: return "SYNC/EMCY"
    if 0x180 <= cobid <= 0x1FF: return "TPDO1"
    if 0x200 <= cobid <= 0x27F: return "RPDO1"
    if 0x580 <= cobid <= 0x5FF: return "SDO-rsp"
    if 0x600 <= cobid <= 0x67F: return "SDO-req"
    if 0x700 <= cobid <= 0x77F: return "HB"
    return "?"


class TelemetryHub:
    def __init__(self):
        self.slaves = [(j, CiA402Slave(j["node"], j["model"], j["name"])) for j in JOINTS]
        self.lock = threading.Lock()
        self.manual = set()
        self.frames_rx = 0
        self.frames_tx = 0
        self.source = "idle"
        self.t0 = time.time()
        # CANopen 資料流追蹤（只記選取那顆 motor）
        self.trace = ("L", 1)                       # (arm, node)
        self.frame_log = collections.deque(maxlen=400)
        self.trace_rx = 0
        self.trace_tx = 0

    def _key(self, arm, node):
        for i, (j, _) in enumerate(self.slaves):
            if j["arm"] == arm and j["node"] == node:
                return i
        return None

    # ---- 記一幀（only 追蹤目標的 node）----
    def log_frame(self, direction, node, cobid, data):
        if node != self.trace[1]:
            return
        if direction == "RX":
            self.trace_rx += 1
        else:
            self.trace_tx += 1
        self.frame_log.append(dict(
            t=round(time.time() - self.t0, 3), dir=direction,
            kind=frame_kind(cobid), cobid="0x%03X" % cobid,
            hex=" ".join("%02X" % b for b in data)))

    def snapshot(self):
        out = []
        with self.lock:
            for j, s in self.slaves:
                m = s.motor
                out.append(dict(
                    arm=j["arm"], node=j["node"], model=j["model"], name=j["name"],
                    pos=round(m.q, 4), vel=round(m.qd, 4),
                    sw=m.statusword, state=decode_sw(m.statusword),
                    mode=MODE_NAMES.get(m.mode, str(m.mode)),
                    enabled=bool(m.enabled), fault=(m.state == 7),
                    torque=round(m.torque, 3), current=round(m.current, 3),
                    peak_torque=m.peak_torque, rated_current=m.rated_current))
            frames = list(self.frame_log)[-60:]
        return dict(t=round(time.time() - self.t0, 2), source=self.source,
                    rx=self.frames_rx, tx=self.frames_tx,
                    trace=dict(arm=self.trace[0], node=self.trace[1],
                               rx=self.trace_rx, tx=self.trace_tx),
                    frames=frames, joints=out)

    def apply_command(self, arm, node, action, value):
        i = self._key(arm, node)
        if i is None:
            return False, "no such joint"
        j, s = self.slaves[i]
        m = s.motor
        with self.lock:
            if action == "trace":                      # 切換資料流追蹤對象
                self.trace = (arm, node)
                self.frame_log.clear(); self.trace_rx = self.trace_tx = 0
                return True, "ok"
            self.manual.add(i)
            if action == "mode":
                m.write_od(0x6060, 0, int(value)); m.mode = int(value)
            elif action == "enable":
                for cw in (0x06, 0x07, 0x0F):
                    m.apply_controlword(cw)
            elif action == "disable":
                m.apply_controlword(0x00)
            elif action == "quickstop":
                m.apply_controlword(0x02)
            elif action == "faultreset":
                for cw in (0x80, 0x06, 0x07, 0x0F):     # 清故障並重新使能
                    m.apply_controlword(cw)
            elif action == "halt":
                m.apply_controlword(0x10F)
            elif action == "fault":
                m.inject_fault()
            elif action == "home":
                m.write_od(0x6060, 0, 6); m.mode = 6
                for cw in (0x06, 0x07, 0x0F):
                    m.apply_controlword(cw)
                m.apply_controlword(0x1F)
            elif action == "target":
                v = float(value)
                if m.mode in (1, 8):
                    m.write_od(0x607A, 0, int(v * CPR) & 0xFFFFFFFF)
                elif m.mode in (3, 9):
                    m.write_od(0x60FF, 0, int(v * CPR) & 0xFFFFFFFF)
                elif m.mode in (4, 10, 13):
                    m.write_od(0x6071, 0, int(v) & 0xFFFF)
            elif action == "auto":
                self.manual.discard(i)
            else:
                return False, "unknown action"
        return True, "ok"

    def _traced_slave(self):
        i = self._key(*self.trace)
        return self.slaves[i][1] if i is not None else None

    # ---- demo：自驅 + 對追蹤 motor 產生 CANopen 幀交換 ----
    def run_demo(self, hz=200):
        self.source = "demo"
        for _, s in self.slaves:
            s.motor.write_od(0x6060, 0, 8)
            for cw in (0x06, 0x07, 0x0F):
                s.motor.apply_controlword(cw)
        dt = 1.0 / hz
        c = 0
        while True:
            t = time.time() - self.t0
            c += 1
            with self.lock:
                for i, (j, s) in enumerate(self.slaves):
                    mm = s.motor
                    if i not in self.manual and mm.mode == 8:
                        amp = 0.6 if j["model"] == "PHU20" else (0.9 if j["model"] == "PHU17" else 1.2)
                        mm.target_counts = int(amp * math.sin(0.4 * t + i * 0.5) * CPR)
                    mm.step(dt)
                    self.frames_rx += 1; self.frames_tx += 1
                # 對追蹤的那顆每 10 cycle（~20Hz）產生一次 PDO 交換幀
                if c % 10 == 0:
                    s = self._traced_slave()
                    if s:
                        self._demo_exchange(s, sdo=(c % 200 == 0))
            time.sleep(dt)

    def _demo_exchange(self, s, sdo=False):
        """產生並記錄一次 RPDO1→TPDO1（與週期性 SDO 讀）幀。"""
        m = s.motor
        node = m.node_id
        # 週期性 SDO 讀（statusword / 實際位置），示意主站輪詢
        if sdo:
            for idx in (0x6041, 0x6064):
                req = [0x40, idx & 0xFF, idx >> 8, 0, 0, 0, 0, 0]
                self.log_frame("RX", node, SDO_RX + node, req)
                for arb, data in s.handle_frame(SDO_RX + node, req):
                    self.log_frame("TX", node, arb, data)
        # 週期 PDO：RPDO1 [CW][target32]（不再經 handle_frame 以免重複步進）
        cw = m.controlword & 0xFFFF
        tc = m.target_counts & 0xFFFFFFFF
        rpdo = [cw & 0xFF, cw >> 8, tc & 0xFF, (tc >> 8) & 0xFF, (tc >> 16) & 0xFF, (tc >> 24) & 0xFF]
        self.log_frame("RX", node, RPDO1 + node, rpdo)
        sw = m.statusword & 0xFFFF
        ap = m.read_od(0x6064) & 0xFFFFFFFF
        tpdo = [sw & 0xFF, sw >> 8, ap & 0xFF, (ap >> 8) & 0xFF, (ap >> 16) & 0xFF, (ap >> 24) & 0xFF]
        self.log_frame("TX", node, TPDO1 + node, tpdo)

    # ---- 真機：python-can（記錄真實 bus 幀）----
    def run_can(self, interface, channel, bitrate, hb_ms, step_hz=200):
        try:
            import can
        except ImportError:
            raise SystemExit("找不到 python-can。請先 pip install python-can gs_usb")
        kwargs = dict(interface=interface, bitrate=bitrate)
        if channel is not None:
            kwargs["channel"] = channel
        bus = can.Bus(**kwargs)
        self.source = "canable:%s" % interface

        def stepper():
            dt = 1.0 / step_hz
            while True:
                with self.lock:
                    for _, s in self.slaves:
                        if s.motor.state in (4, 5):
                            s.motor.step(dt)
                time.sleep(dt)
        threading.Thread(target=stepper, daemon=True).start()
        for _, s in self.slaves:
            bus.send(can.Message(arbitration_id=HEARTBEAT + s.node_id, data=[0x00],
                                 is_extended_id=False))
        while True:
            msg = bus.recv(timeout=0.1)
            if msg is None or msg.is_extended_id:
                continue
            self.frames_rx += 1
            node = msg.arbitration_id & 0x7F
            with self.lock:
                self.log_frame("RX", node, msg.arbitration_id, list(msg.data))
                for _, s in self.slaves:
                    for arb, data in s.handle_frame(msg.arbitration_id, msg.data):
                        bus.send(can.Message(arbitration_id=arb, data=bytes(data),
                                             is_extended_id=False))
                        self.frames_tx += 1
                        self.log_frame("TX", arb & 0x7F, arb, data)


# ============================ 前端 ============================
INDEX_HTML = r"""<!doctype html><html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>EYOU 雙臂 14 軸 上位機</title><style>
 :root{--bg:#0e1116;--card:#171b22;--line:#262c36;--fg:#e6edf3;--mut:#8b97a7;
  --ok:#3fb950;--warn:#d29922;--bad:#f85149;--accent:#58a6ff;--rx:#58a6ff;--tx:#3fb950}
 *{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);
  font:13px/1.4 ui-sans-serif,system-ui,"Segoe UI",sans-serif}
 header{display:flex;align-items:center;gap:14px;flex-wrap:wrap;padding:10px 16px;
  border-bottom:1px solid var(--line);position:sticky;top:0;background:var(--bg);z-index:9}
 header h1{font-size:15px;margin:0;font-weight:600}.pill{font-size:12px;color:var(--mut)}
 .pill b{color:var(--fg)}#dot{width:9px;height:9px;border-radius:50%;background:var(--bad);
  display:inline-block;margin-right:6px}#dot.on{background:var(--ok)}
 .ctl{display:flex;gap:6px;align-items:center;flex-wrap:wrap;padding:9px 16px;
  border-bottom:1px solid var(--line);background:#10141b;position:sticky;top:43px;z-index:8}
 select,input,button{background:#0d1117;color:var(--fg);border:1px solid var(--line);
  border-radius:6px;padding:4px 8px;font:inherit}button{cursor:pointer}button:hover{border-color:var(--accent)}
 button.en{color:var(--ok)}button.stop{color:var(--warn)}button.flt{color:var(--bad)}
 .wrap{display:grid;grid-template-columns:1fr 360px;gap:12px;padding:12px}
 @media(max-width:980px){.wrap{grid-template-columns:1fr}}
 .arms{display:grid;grid-template-columns:1fr 1fr;gap:10px}
 @media(max-width:620px){.arms{grid-template-columns:1fr}}
 .arm h2{font-size:12px;color:var(--mut);margin:0 0 7px;font-weight:600}
 .card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:7px 9px;margin-bottom:6px}
 .card.sel{border-color:var(--accent)}
 .row1{display:flex;align-items:center;justify-content:space-between;margin-bottom:5px;cursor:pointer}
 .name{font-weight:600}.model{color:var(--mut);font-size:11px;margin-left:5px}
 .tags{display:flex;gap:5px}.badge{font-size:11px;padding:1px 7px;border-radius:10px;background:#21262d;color:var(--mut)}
 .badge.en{background:rgba(63,185,80,.15);color:var(--ok)}.badge.flt{background:rgba(248,81,73,.15);color:var(--bad)}
 .badge.md{background:rgba(88,166,255,.15);color:var(--accent)}
 .metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
 .m label{display:block;color:var(--mut);font-size:10px}.m .v{font-variant-numeric:tabular-nums}
 .bar{height:5px;background:#0b0e13;border-radius:3px;overflow:hidden;margin-top:3px}
 .bar i{display:block;height:100%;background:var(--accent);width:0}.bar.tq i{background:var(--warn)}.bar.cu i{background:var(--ok)}
 .trace{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:8px 10px;
  position:sticky;top:96px;height:calc(100vh - 120px);display:flex;flex-direction:column}
 .trace h3{margin:0 0 6px;font-size:13px}.trace .sub{color:var(--mut);font-size:11px;margin-bottom:6px}
 .log{flex:1;overflow:auto;font:11px/1.5 ui-monospace,Consolas,monospace}
 .fr{display:flex;gap:6px;padding:1px 0;white-space:nowrap}
 .fr .d{width:22px;font-weight:700}.fr.rx .d{color:var(--rx)}.fr.tx .d{color:var(--tx)}
 .fr .k{width:62px;color:var(--mut)}.fr .id{width:46px;color:var(--accent)}.fr .h{color:var(--fg)}
 .fr .ts{width:48px;color:#5b6573}
</style></head><body>
<header><h1>EYOU 雙臂 · 14 軸上位機</h1>
 <span class="pill"><span id="dot"></span><span id="conn">連線中…</span></span>
 <span class="pill">來源 <b id="src">—</b></span><span class="pill">t <b id="t">0</b>s</span>
 <span class="pill">總 RX <b id="rx">0</b>·TX <b id="tx">0</b></span></header>
<div class="ctl">
 <span class="pill">控制 <b id="selj">L_J1</b></span>
 <select id="mode" title="0x6060 運動模式">
  <option value="1">PP 輪廓位置</option><option value="3">PV 輪廓速度</option>
  <option value="4">PT 輪廓力矩</option><option value="6">HM 回零</option>
  <option value="8" selected>CSP 同步位置</option><option value="9">CSV 同步速度</option>
  <option value="10">CST 同步力矩</option><option value="13">CSF 同步力控</option></select>
 <button class="en" data-a="enable">使能</button><button data-a="disable">解除</button>
 <button class="stop" data-a="quickstop">急停</button><button class="stop" data-a="halt">Halt</button>
 <button data-a="home">回零</button><button class="flt" data-a="fault">注入故障</button>
 <button class="en" data-a="faultreset">清故障</button>
 <input id="tgt" type="number" step="0.05" placeholder="目標值" style="width:84px">
 <button data-a="target">設定</button><span class="pill" id="thint">rad</span>
 <button data-a="auto" title="交還 demo 自驅">自動</button></div>
<div class="wrap">
 <div><div class="arms">
  <div class="arm"><h2>◀ 左臂 LEFT (CAN1)</h2><div id="L"></div></div>
  <div class="arm"><h2>右臂 RIGHT (CAN2) ▶</h2><div id="R"></div></div></div></div>
 <div class="trace"><h3>CAN 資料流 · <span id="tnode">L_J1</span></h3>
  <div class="sub">從站視角　<span style="color:var(--rx)">RX</span> 主站→從站
   <span style="color:var(--tx)">TX</span> 從站→主站　|　RX <b id="trx">0</b> · TX <b id="ttx">0</b></div>
  <div class="log" id="log"></div></div>
</div>
<script>
const cards={};let sel={arm:"L",node:1,name:"L_J1"};
const HINT={1:"rad",8:"rad",3:"rad/s",9:"rad/s",4:"‰額定",10:"‰額定",13:"‰額定",6:"—"};
function clamp(x){return Math.max(0,Math.min(1,x))}
function post(action,value){return fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({arm:sel.arm,node:sel.node,action:action,value:value||""})});}
function pick(j){sel={arm:j.arm,node:j.node,name:j.name};
 document.getElementById('selj').textContent=j.name;document.getElementById('tnode').textContent=j.name;
 Object.values(cards).forEach(c=>c.el.classList.remove('sel'));
 if(cards[j.name])cards[j.name].el.classList.add('sel');
 post('trace');document.getElementById('log').innerHTML='';}
function ensure(j){if(cards[j.name])return cards[j.name];
 const el=document.createElement('div');el.className='card';
 el.innerHTML=`<div class="row1"><div><span class="name"></span><span class="model"></span></div>
  <div class="tags"><span class="badge md"></span><span class="badge st"></span></div></div>
  <div class="metrics">
   <div class="m"><label>位置 rad</label><div class="v p"></div><div class="bar"><i class="pb"></i></div></div>
   <div class="m"><label>扭矩 N·m</label><div class="v tq"></div><div class="bar tq"><i class="tb"></i></div></div>
   <div class="m"><label>電流 A</label><div class="v cu"></div><div class="bar cu"><i class="cb"></i></div></div></div>`;
 document.getElementById(j.arm).appendChild(el);el.querySelector('.row1').onclick=()=>pick(j);
 const c={el,name:el.querySelector('.name'),model:el.querySelector('.model'),md:el.querySelector('.md'),
  st:el.querySelector('.st'),p:el.querySelector('.p'),pb:el.querySelector('.pb'),tq:el.querySelector('.tq'),
  tb:el.querySelector('.tb'),cu:el.querySelector('.cu'),cb:el.querySelector('.cb')};
 cards[j.name]=c;return c;}
function upd(j){const c=ensure(j);c.name.textContent=j.name;c.model.textContent=j.model;
 c.md.textContent=j.mode;c.st.textContent=j.state;
 c.st.className='badge st'+(j.state==='operation-enabled'?' en':(j.fault?' flt':''));
 c.p.textContent=j.pos.toFixed(3);c.pb.style.width=(clamp((j.pos+Math.PI)/(2*Math.PI))*100)+'%';
 c.tq.textContent=j.torque.toFixed(2);c.tb.style.width=(clamp(Math.abs(j.torque)/j.peak_torque)*100)+'%';
 c.cu.textContent=j.current.toFixed(2);c.cb.style.width=(clamp(Math.abs(j.current)/j.rated_current)*100)+'%';}
function renderFrames(d){document.getElementById('trx').textContent=d.trace.rx;
 document.getElementById('ttx').textContent=d.trace.tx;
 const log=document.getElementById('log');
 log.innerHTML=d.frames.slice().reverse().map(f=>
  `<div class="fr ${f.dir.toLowerCase()}"><span class="ts">${f.t.toFixed(2)}</span>`+
  `<span class="d">${f.dir}</span><span class="k">${f.kind}</span>`+
  `<span class="id">${f.cobid}</span><span class="h">${f.hex}</span></div>`).join('');}
document.querySelectorAll('.ctl button').forEach(b=>b.onclick=()=>post(b.dataset.a,document.getElementById('tgt').value));
document.getElementById('mode').onchange=e=>{post('mode',e.target.value);
 document.getElementById('thint').textContent=HINT[e.target.value]||'';};
const es=new EventSource('/events');
es.onopen=()=>{document.getElementById('dot').classList.add('on');document.getElementById('conn').textContent='已連線';};
es.onerror=()=>{document.getElementById('dot').classList.remove('on');document.getElementById('conn').textContent='斷線，重連中…';};
es.onmessage=e=>{const d=JSON.parse(e.data);
 document.getElementById('src').textContent=d.source;document.getElementById('t').textContent=d.t;
 document.getElementById('rx').textContent=d.rx;document.getElementById('tx').textContent=d.tx;
 d.joints.forEach(upd);renderFrames(d);};
</script></body></html>"""


def make_handler(hub, rate_hz):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if path == "/" or path.startswith("/index"):
                body = INDEX_HTML.encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif path == "/events":
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache")
                self.end_headers()
                period = 1.0 / rate_hz
                try:
                    while True:
                        data = json.dumps(hub.snapshot(), ensure_ascii=False)
                        self.wfile.write(("data: %s\n\n" % data).encode("utf-8"))
                        self.wfile.flush()
                        time.sleep(period)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
                    pass
            else:
                self.send_error(404)

        def do_POST(self):
            if self.path != "/cmd":
                self.send_error(404); return
            n = int(self.headers.get("Content-Length", 0))
            try:
                req = json.loads(self.rfile.read(n) or b"{}")
                ok, msg = hub.apply_command(req.get("arm"), int(req.get("node")),
                                            req.get("action"), req.get("value"))
                code = 200 if ok else 400
            except Exception as e:
                code, msg = 400, str(e)
            body = json.dumps({"ok": code == 200, "msg": msg}).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
    return H


def main():
    ap = argparse.ArgumentParser(description="C1 雙臂 14 軸 web 上位機 + CANopen 資料流")
    ap.add_argument("--demo", action="store_true")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--rate", type=float, default=15.0)
    ap.add_argument("--interface", default="gs_usb")
    ap.add_argument("--channel", default=None)
    ap.add_argument("--bitrate", type=int, default=1000000)
    ap.add_argument("--heartbeat", type=int, default=0)
    args = ap.parse_args()

    hub = TelemetryHub()
    if args.demo:
        threading.Thread(target=hub.run_demo, daemon=True).start()
    else:
        threading.Thread(target=lambda: hub.run_can(args.interface, args.channel,
                         args.bitrate, args.heartbeat), daemon=True).start()
    httpd = ThreadingHTTPServer((args.host, args.port), make_handler(hub, args.rate))
    print("上位機啟動： http://%s:%d   （%s，Ctrl-C 結束）"
          % (args.host, args.port, "demo" if args.demo else "canable"))
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n結束。")


if __name__ == "__main__":
    main()
