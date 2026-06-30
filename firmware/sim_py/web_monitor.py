"""
web_monitor.py — C1 上位機 web 看板（純標準庫，零第三方相依）

把雙臂 14 軸從站的即時遙測（位置 / 扭矩 / 電流 / CiA402 狀態）推到瀏覽器畫面。
後端 = stdlib http.server（ThreadingHTTPServer）+ SSE（text/event-stream）；
前端 = 單頁 vanilla JS，EventSource 訂閱 /events，~15 Hz 重繪。

兩種資料來源
------------
  # 1) demo：無任何硬體，後端自己驅動 14 顆模擬關節 → 看板立刻會動
  python web_monitor.py --demo
  # 開 http://127.0.0.1:8080

  # 2) 真機（C1）：F746 主站透過 CANable 驅動 PC 上的從站，看板顯示從站即時狀態
  #    先 pip install python-can gs_usb（Windows 用 Zadig 換 WinUSB），或 WSL socketcan
  python web_monitor.py --interface gs_usb --channel 0 --bitrate 1000000

關節對應（依 CLAUDE.md）：每臂 J1,J2=PHU20 / J3,J4=PHU17 / J5,J6,J7=PHU14。
"""
import argparse
import json
import math
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from can_slave import CiA402Slave

# ---- 雙臂關節配置：(arm, bus, node, model, 名稱) ----
JOINTS = []
for arm, bus in (("L", "L"), ("R", "R")):
    for node, model, part in [
        (1, "PHU20", "Shoulder"), (2, "PHU20", "Shoulder"), (3, "PHU17", "Shoulder-Yaw"),
        (4, "PHU17", "Elbow"), (5, "PHU14", "Wrist1"), (6, "PHU14", "Wrist2"),
        (7, "PHU14", "Wrist3"),
    ]:
        JOINTS.append(dict(arm=arm, bus=bus, node=node, model=model,
                           name="%s_J%d_%s" % (arm, node, part)))

# CiA402 狀態字 → 名稱（顯示用）
SW_NAMES = {
    0x0040: "switch-on-disabled",
    0x0021: "ready-to-switch-on",
    0x0023: "switched-on",
    0x0027: "operation-enabled",
    0x0008: "fault",
}
CPR = 524288.0 / (2.0 * math.pi)


def decode_sw(sw):
    return SW_NAMES.get(sw & 0x006F, "0x%04X" % sw)


class TelemetryHub:
    """持有 14 顆從站；提供即時遙測快照。可由 demo 內部驅動或由真實 CAN 驅動。"""

    def __init__(self):
        # 每顆關節一個 CiA402Slave（內含一顆 PhuMotor）
        self.slaves = []
        for j in JOINTS:
            s = CiA402Slave(j["node"], j["model"], j["name"])
            self.slaves.append((j, s))
        self.lock = threading.Lock()
        self.frames_rx = 0
        self.frames_tx = 0
        self.source = "idle"
        self.t0 = time.time()

    # ---- 遙測快照（給 SSE 推送）----
    def snapshot(self):
        out = []
        with self.lock:
            for j, s in self.slaves:
                m = s.motor
                out.append(dict(
                    arm=j["arm"], node=j["node"], model=j["model"], name=j["name"],
                    pos=round(m.q, 4),
                    vel=round(m.qd, 4),
                    sw=m.statusword,
                    state=decode_sw(m.statusword),
                    enabled=bool(m.enabled),
                    torque=round(m.torque, 3),
                    current=round(m.current, 3),
                    peak_torque=m.peak_torque,
                    rated_current=m.rated_current,
                ))
        return dict(t=round(time.time() - self.t0, 2), source=self.source,
                    rx=self.frames_rx, tx=self.frames_tx, joints=out)

    # ---- demo：內部主站驅動（CSP 正弦），讓看板會動 ----
    def run_demo(self, hz=200):
        self.source = "demo"
        for _, s in self.slaves:
            s.motor.mode = 8                    # CSP
            s.motor.apply_controlword(0x000F)   # operation-enabled
        dt = 1.0 / hz
        while True:
            t = time.time() - self.t0
            with self.lock:
                for idx, (j, s) in enumerate(self.slaves):
                    amp = 0.6 if j["model"] == "PHU20" else (0.9 if j["model"] == "PHU17" else 1.2)
                    phase = idx * 0.5
                    target_rad = amp * math.sin(0.4 * t + phase)
                    s.motor.target_counts = int(target_rad * CPR)
                    s.motor.step(dt)
                    self.frames_rx += 1
                    self.frames_tx += 1
            time.sleep(dt)

    # ---- 真機：python-can 收 F746 主站的 frame → 餵從站 → 回應 ----
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
        by_node = {s.node_id: s for _, s in self.slaves}   # 注意：左右臂同 node 號

        # 背景：對 enabled 的馬達持續步進，讓 PV/CSP 動畫流暢
        def stepper():
            dt = 1.0 / step_hz
            while True:
                with self.lock:
                    for _, s in self.slaves:
                        if s.motor.enabled:
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


# ============================ HTTP / SSE ============================
INDEX_HTML = r"""<!doctype html>
<html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>EYOU 雙臂 14 軸 即時看板</title>
<style>
  :root{--bg:#0e1116;--card:#171b22;--line:#262c36;--fg:#e6edf3;--mut:#8b97a7;
        --ok:#3fb950;--warn:#d29922;--bad:#f85149;--accent:#58a6ff;}
  *{box-sizing:border-box} body{margin:0;background:var(--bg);color:var(--fg);
    font:13px/1.4 ui-sans-serif,system-ui,"Segoe UI",sans-serif}
  header{display:flex;align-items:center;gap:16px;padding:12px 18px;
    border-bottom:1px solid var(--line);position:sticky;top:0;background:var(--bg);z-index:5}
  header h1{font-size:15px;margin:0;font-weight:600}
  .pill{font-size:12px;color:var(--mut)} .pill b{color:var(--fg)}
  #dot{width:9px;height:9px;border-radius:50%;background:var(--bad);display:inline-block;margin-right:6px}
  #dot.on{background:var(--ok)}
  .arms{display:grid;grid-template-columns:1fr 1fr;gap:14px;padding:14px}
  @media(max-width:780px){.arms{grid-template-columns:1fr}}
  .arm h2{font-size:13px;color:var(--mut);margin:0 0 8px;font-weight:600;letter-spacing:.05em}
  .card{background:var(--card);border:1px solid var(--line);border-radius:8px;
    padding:9px 11px;margin-bottom:8px}
  .row1{display:flex;align-items:center;justify-content:space-between;margin-bottom:6px}
  .name{font-weight:600} .model{color:var(--mut);font-size:11px;margin-left:6px}
  .badge{font-size:11px;padding:2px 7px;border-radius:10px;background:#21262d;color:var(--mut)}
  .badge.en{background:rgba(63,185,80,.15);color:var(--ok)}
  .badge.flt{background:rgba(248,81,73,.15);color:var(--bad)}
  .metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
  .m label{display:block;color:var(--mut);font-size:10px;margin-bottom:2px}
  .m .v{font-variant-numeric:tabular-nums;font-size:12px}
  .bar{height:5px;background:#0b0e13;border-radius:3px;overflow:hidden;margin-top:3px}
  .bar i{display:block;height:100%;background:var(--accent);width:0}
  .bar.tq i{background:var(--warn)} .bar.cu i{background:var(--ok)}
</style></head>
<body>
<header>
  <h1>EYOU 雙臂 · 14 軸即時看板</h1>
  <span class="pill"><span id="dot"></span><span id="conn">連線中…</span></span>
  <span class="pill">來源 <b id="src">—</b></span>
  <span class="pill">t <b id="t">0</b>s</span>
  <span class="pill">RX <b id="rx">0</b> · TX <b id="tx">0</b></span>
</header>
<div class="arms">
  <div class="arm"><h2>◀ 左臂 LEFT (bus CAN1)</h2><div id="L"></div></div>
  <div class="arm"><h2>右臂 RIGHT (bus CAN2) ▶</h2><div id="R"></div></div>
</div>
<script>
const cards={};
function clamp(x){return Math.max(0,Math.min(1,x));}
function ensure(j){
  if(cards[j.name]) return cards[j.name];
  const el=document.createElement('div'); el.className='card';
  el.innerHTML=`<div class="row1"><div><span class="name"></span><span class="model"></span></div>
    <span class="badge st"></span></div>
    <div class="metrics">
      <div class="m"><label>位置 rad</label><div class="v p"></div><div class="bar"><i class="pb"></i></div></div>
      <div class="m"><label>扭矩 N·m</label><div class="v tq"></div><div class="bar tq"><i class="tb"></i></div></div>
      <div class="m"><label>電流 A</label><div class="v cu"></div><div class="bar cu"><i class="cb"></i></div></div>
    </div>`;
  document.getElementById(j.arm).appendChild(el);
  const c={el,name:el.querySelector('.name'),model:el.querySelector('.model'),
    st:el.querySelector('.st'),p:el.querySelector('.p'),pb:el.querySelector('.pb'),
    tq:el.querySelector('.tq'),tb:el.querySelector('.tb'),
    cu:el.querySelector('.cu'),cb:el.querySelector('.cb')};
  cards[j.name]=c; return c;
}
function upd(j){
  const c=ensure(j);
  c.name.textContent=j.name; c.model.textContent=j.model;
  c.st.textContent=j.state;
  c.st.className='badge st'+(j.state==='operation-enabled'?' en':(j.state==='fault'?' flt':''));
  c.p.textContent=j.pos.toFixed(3);
  c.pb.style.width=(clamp((j.pos+Math.PI)/(2*Math.PI))*100)+'%';
  c.tq.textContent=j.torque.toFixed(2);
  c.tb.style.width=(clamp(Math.abs(j.torque)/j.peak_torque)*100)+'%';
  c.cu.textContent=j.current.toFixed(2);
  c.cb.style.width=(clamp(Math.abs(j.current)/j.rated_current)*100)+'%';
}
const es=new EventSource('/events');
es.onopen=()=>{document.getElementById('dot').classList.add('on');
  document.getElementById('conn').textContent='已連線';};
es.onerror=()=>{document.getElementById('dot').classList.remove('on');
  document.getElementById('conn').textContent='斷線，重連中…';};
es.onmessage=e=>{const d=JSON.parse(e.data);
  document.getElementById('src').textContent=d.source;
  document.getElementById('t').textContent=d.t;
  document.getElementById('rx').textContent=d.rx;
  document.getElementById('tx').textContent=d.tx;
  d.joints.forEach(upd);
};
</script></body></html>"""


def make_handler(hub, rate_hz):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass   # 安靜

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
                self.send_header("Connection", "keep-alive")
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
    return H


def main():
    ap = argparse.ArgumentParser(description="C1 雙臂 14 軸 web 即時看板")
    ap.add_argument("--demo", action="store_true", help="無硬體：後端自驅 14 軸")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--rate", type=float, default=15.0, help="SSE 推送頻率 Hz")
    ap.add_argument("--interface", default="gs_usb", help="真機 python-can interface")
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
    print("看板已啟動： http://%s:%d   （來源：%s，Ctrl-C 結束）"
          % (args.host, args.port, "demo" if args.demo else "canable"))
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n結束。")


if __name__ == "__main__":
    main()
