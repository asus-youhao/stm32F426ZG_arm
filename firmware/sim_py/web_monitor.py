"""
web_monitor.py — C1 上位機 web 看板 + 控制台（純標準庫，零第三方相依）

雙臂 14 軸即時遙測（位置/扭矩/電流/CiA402 狀態）+ 從瀏覽器**切換 8 種模式、下控制字、
設目標、注入/清除故障**。後端 = stdlib http.server（SSE 推遙測 + POST /cmd 收命令）。

  # demo：無硬體，後端自驅 14 軸（可被手動命令接管）
  python web_monitor.py --demo            # 開 http://127.0.0.1:8080

  # 真機（C1）：F746 主站經 CANable 驅動 PC 從站
  python web_monitor.py --interface gs_usb --channel 0 --bitrate 1000000
"""
import argparse
import json
import math
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from phu_motor import PhuMotor, CPR
from can_slave import CiA402Slave

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


class TelemetryHub:
    def __init__(self):
        self.slaves = [(j, CiA402Slave(j["node"], j["model"], j["name"])) for j in JOINTS]
        self.lock = threading.Lock()
        self.manual = set()          # 被手動命令接管的關節 index（demo 不再自驅）
        self.frames_rx = 0
        self.frames_tx = 0
        self.source = "idle"
        self.t0 = time.time()

    def _key(self, arm, node):
        for i, (j, _) in enumerate(self.slaves):
            if j["arm"] == arm and j["node"] == node:
                return i
        return None

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
        return dict(t=round(time.time() - self.t0, 2), source=self.source,
                    rx=self.frames_rx, tx=self.frames_tx, joints=out)

    # ---- 來自瀏覽器的命令 ----
    def apply_command(self, arm, node, action, value):
        i = self._key(arm, node)
        if i is None:
            return False, "no such joint"
        j, s = self.slaves[i]
        m = s.motor
        with self.lock:
            self.manual.add(i)        # 接管：demo 停止自驅這顆
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
                m.apply_controlword(0x80); m.apply_controlword(0x06)
            elif action == "halt":
                m.apply_controlword(0x10F)
            elif action == "fault":
                m.inject_fault()
            elif action == "home":
                m.write_od(0x6060, 0, 6); m.mode = 6
                for cw in (0x06, 0x07, 0x0F):
                    m.apply_controlword(cw)
                m.apply_controlword(0x1F)          # bit4 啟動回零
            elif action == "target":
                v = float(value)
                if m.mode in (1, 8):               # 位置模式：rad → counts
                    m.write_od(0x607A, 0, int(v * CPR) & 0xFFFFFFFF)
                elif m.mode in (3, 9):             # 速度模式：rad/s → counts/s
                    m.write_od(0x60FF, 0, int(v * CPR) & 0xFFFFFFFF)
                elif m.mode in (4, 10, 13):        # 力矩模式：‰ 額定
                    m.write_od(0x6071, 0, int(v) & 0xFFFF)
            elif action == "auto":
                self.manual.discard(i)             # 交還 demo 自驅
            else:
                return False, "unknown action"
        return True, "ok"

    # ---- demo：自驅未被接管的關節 ----
    def run_demo(self, hz=200):
        self.source = "demo"
        for i, (_, s) in enumerate(self.slaves):
            s.motor.write_od(0x6060, 0, 8)         # CSP
            for cw in (0x06, 0x07, 0x0F):          # 正規 enable 序列
                s.motor.apply_controlword(cw)
        dt = 1.0 / hz
        while True:
            t = time.time() - self.t0
            with self.lock:
                for i, (j, s) in enumerate(self.slaves):
                    mm = s.motor
                    if i not in self.manual and mm.mode == 8:
                        amp = 0.6 if j["model"] == "PHU20" else (0.9 if j["model"] == "PHU17" else 1.2)
                        mm.target_counts = int(amp * math.sin(0.4 * t + i * 0.5) * CPR)
                    mm.step(dt)
                    self.frames_rx += 1; self.frames_tx += 1
            time.sleep(dt)

    # ---- 真機：python-can ----
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
                        if s.motor.state in (4, 5):   # op-enabled / quick-stop
                            s.motor.step(dt)
                time.sleep(dt)
        threading.Thread(target=stepper, daemon=True).start()
        for _, s in self.slaves:
            bus.send(can.Message(arbitration_id=0x700 + s.node_id, data=[0x00],
                                 is_extended_id=False))
        while True:
            msg = bus.recv(timeout=0.1)
            if msg is None or msg.is_extended_id:
                continue
            self.frames_rx += 1
            with self.lock:
                for _, s in self.slaves:
                    for arb, data in s.handle_frame(msg.arbitration_id, msg.data):
                        bus.send(can.Message(arbitration_id=arb, data=bytes(data),
                                             is_extended_id=False))
                        self.frames_tx += 1


# ============================ 前端 ============================
INDEX_HTML = r"""<!doctype html><html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>EYOU 雙臂 14 軸 上位機</title><style>
 :root{--bg:#0e1116;--card:#171b22;--line:#262c36;--fg:#e6edf3;--mut:#8b97a7;
  --ok:#3fb950;--warn:#d29922;--bad:#f85149;--accent:#58a6ff}
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
  border-radius:6px;padding:4px 8px;font:inherit}
 button{cursor:pointer}button:hover{border-color:var(--accent)}
 button.en{color:var(--ok)}button.stop{color:var(--warn)}button.flt{color:var(--bad)}
 .arms{display:grid;grid-template-columns:1fr 1fr;gap:12px;padding:12px}
 @media(max-width:780px){.arms{grid-template-columns:1fr}}
 .arm h2{font-size:12px;color:var(--mut);margin:0 0 7px;font-weight:600;letter-spacing:.05em}
 .card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:8px 10px;margin-bottom:7px}
 .card.sel{border-color:var(--accent)}
 .row1{display:flex;align-items:center;justify-content:space-between;margin-bottom:5px;cursor:pointer}
 .name{font-weight:600}.model{color:var(--mut);font-size:11px;margin-left:5px}
 .tags{display:flex;gap:5px}.badge{font-size:11px;padding:1px 7px;border-radius:10px;background:#21262d;color:var(--mut)}
 .badge.en{background:rgba(63,185,80,.15);color:var(--ok)}.badge.flt{background:rgba(248,81,73,.15);color:var(--bad)}
 .badge.md{background:rgba(88,166,255,.15);color:var(--accent)}
 .metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:7px}
 .m label{display:block;color:var(--mut);font-size:10px}.m .v{font-variant-numeric:tabular-nums}
 .bar{height:5px;background:#0b0e13;border-radius:3px;overflow:hidden;margin-top:3px}
 .bar i{display:block;height:100%;background:var(--accent);width:0}.bar.tq i{background:var(--warn)}.bar.cu i{background:var(--ok)}
</style></head><body>
<header><h1>EYOU 雙臂 · 14 軸上位機</h1>
 <span class="pill"><span id="dot"></span><span id="conn">連線中…</span></span>
 <span class="pill">來源 <b id="src">—</b></span><span class="pill">t <b id="t">0</b>s</span>
 <span class="pill">RX <b id="rx">0</b>·TX <b id="tx">0</b></span></header>
<div class="ctl">
 <span class="pill">控制 <b id="selj">L_J1</b></span>
 <select id="mode" title="0x6060 運動模式">
  <option value="1">PP 輪廓位置</option><option value="3">PV 輪廓速度</option>
  <option value="4">PT 輪廓力矩</option><option value="6">HM 回零</option>
  <option value="8" selected>CSP 同步位置</option><option value="9">CSV 同步速度</option>
  <option value="10">CST 同步力矩</option><option value="13">CSF 同步力控</option></select>
 <button class="en" data-a="enable">使能</button>
 <button data-a="disable">解除</button>
 <button class="stop" data-a="quickstop">急停</button>
 <button class="stop" data-a="halt">Halt</button>
 <button data-a="home">回零</button>
 <button class="flt" data-a="fault">注入故障</button>
 <button class="en" data-a="faultreset">清故障</button>
 <input id="tgt" type="number" step="0.05" placeholder="目標值" style="width:90px">
 <button data-a="target">設定</button>
 <span class="pill" id="thint">rad</span>
 <button data-a="auto" title="交還 demo 自動驅動">自動</button>
</div>
<div class="arms">
 <div class="arm"><h2>◀ 左臂 LEFT (CAN1)</h2><div id="L"></div></div>
 <div class="arm"><h2>右臂 RIGHT (CAN2) ▶</h2><div id="R"></div></div></div>
<script>
const cards={}; let sel={arm:"L",node:1,name:"L_J1"};
const HINT={1:"rad",8:"rad",3:"rad/s",9:"rad/s",4:"‰額定",10:"‰額定",13:"‰額定",6:"—"};
function clamp(x){return Math.max(0,Math.min(1,x))}
function pick(j){sel={arm:j.arm,node:j.node,name:j.name};
 document.getElementById('selj').textContent=j.name;
 Object.values(cards).forEach(c=>c.el.classList.remove('sel'));
 if(cards[j.name])cards[j.name].el.classList.add('sel');}
function ensure(j){if(cards[j.name])return cards[j.name];
 const el=document.createElement('div');el.className='card';
 el.innerHTML=`<div class="row1"><div><span class="name"></span><span class="model"></span></div>
  <div class="tags"><span class="badge md"></span><span class="badge st"></span></div></div>
  <div class="metrics">
   <div class="m"><label>位置 rad</label><div class="v p"></div><div class="bar"><i class="pb"></i></div></div>
   <div class="m"><label>扭矩 N·m</label><div class="v tq"></div><div class="bar tq"><i class="tb"></i></div></div>
   <div class="m"><label>電流 A</label><div class="v cu"></div><div class="bar cu"><i class="cb"></i></div></div></div>`;
 document.getElementById(j.arm).appendChild(el);
 el.querySelector('.row1').onclick=()=>pick(j);
 const c={el,name:el.querySelector('.name'),model:el.querySelector('.model'),
  md:el.querySelector('.md'),st:el.querySelector('.st'),p:el.querySelector('.p'),pb:el.querySelector('.pb'),
  tq:el.querySelector('.tq'),tb:el.querySelector('.tb'),cu:el.querySelector('.cu'),cb:el.querySelector('.cb')};
 cards[j.name]=c;return c;}
function upd(j){const c=ensure(j);c.name.textContent=j.name;c.model.textContent=j.model;
 c.md.textContent=j.mode;c.st.textContent=j.state;
 c.st.className='badge st'+(j.state==='operation-enabled'?' en':(j.fault?' flt':''));
 c.p.textContent=j.pos.toFixed(3);c.pb.style.width=(clamp((j.pos+Math.PI)/(2*Math.PI))*100)+'%';
 c.tq.textContent=j.torque.toFixed(2);c.tb.style.width=(clamp(Math.abs(j.torque)/j.peak_torque)*100)+'%';
 c.cu.textContent=j.current.toFixed(2);c.cb.style.width=(clamp(Math.abs(j.current)/j.rated_current)*100)+'%';}
function cmd(action){const v=document.getElementById('tgt').value;
 fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({arm:sel.arm,node:sel.node,action:action,value:v})});}
document.querySelectorAll('.ctl button').forEach(b=>b.onclick=()=>cmd(b.dataset.a));
document.getElementById('mode').onchange=e=>{
 fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({arm:sel.arm,node:sel.node,action:'mode',value:e.target.value})});
 document.getElementById('thint').textContent=HINT[e.target.value]||'';};
const es=new EventSource('/events');
es.onopen=()=>{document.getElementById('dot').classList.add('on');document.getElementById('conn').textContent='已連線';};
es.onerror=()=>{document.getElementById('dot').classList.remove('on');document.getElementById('conn').textContent='斷線，重連中…';};
es.onmessage=e=>{const d=JSON.parse(e.data);
 document.getElementById('src').textContent=d.source;document.getElementById('t').textContent=d.t;
 document.getElementById('rx').textContent=d.rx;document.getElementById('tx').textContent=d.tx;
 d.joints.forEach(upd);};
</script></body></html>"""


def make_handler(hub, rate_hz):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def do_GET(self):
            if self.path == "/" or self.path.startswith("/index"):
                body = INDEX_HTML.encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path == "/events":
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
                except (BrokenPipeError, ConnectionResetError):
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
    ap = argparse.ArgumentParser(description="C1 雙臂 14 軸 web 上位機")
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
