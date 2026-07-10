#!/usr/bin/env python3
"""ecat_live.py — 即時 EtherCAT sniffer（SSE 串流到瀏覽器,同一套 viewer UI）

用法（需 CAP_NET_RAW,G16 用 python3-rawnet）：
  python3-rawnet tools/ecat_live.py --iface enx00e04c6809b6 [--port 8792]
  → 瀏覽器開 http://localhost:8792  即時看板端主站 ↔ 假從站流量

解碼與取樣邏輯共用 ecat_sniff.py（LRW 只推「內容有變化」者,另每秒放行一幀
心跳樣本,避免 250Hz×2 洗版）。Artifact 版是靜態快照;本檔才是 real-time。
"""
import argparse, http.server, json, os, queue, socket, socketserver, sys, threading, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ecat_sniff import decode_frame                     # noqa: E402

TPL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ecat_bus_viewer_tpl.html")
clients = []          # list[queue.Queue[str]]
clients_lock = threading.Lock()
stat = {"captured": 0, "sent": 0}


def broadcast(obj):
    msg = "data: " + json.dumps(obj, ensure_ascii=False) + "\n\n"
    with clients_lock:
        for q in clients:
            try:
                q.put_nowait(msg)
            except queue.Full:
                pass


def sniffer(iface):
    s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0003))
    s.bind((iface, 0))
    s.settimeout(0.2)
    t0 = time.monotonic()
    last_sig, last_pass = {}, {}
    while True:
        try:
            pkt, addr = s.recvfrom(2048)
        except socket.timeout:
            continue
        if len(pkt) < 16 or pkt[12:14] != b"\x88\xa4":
            continue
        stat["captured"] += 1
        now = time.monotonic()
        is_reply = (addr[2] == socket.PACKET_OUTGOING)
        info, tree, sig, cyclic, can = decode_frame(bytes(pkt), is_reply)
        if cyclic:                                   # 變化才推;每秒放行一幀心跳
            key = is_reply
            if last_sig.get(key) == sig and now - last_pass.get(key, 0) < 1.0:
                continue
            last_sig[key] = sig
            last_pass[key] = now
        stat["sent"] += 1
        broadcast({"t": round(now - t0, 6), "dir": "reply" if is_reply else "master",
                   "len": len(pkt), "info": info, "tree": tree, "cyc": cyclic,
                   "can": can, "hex": pkt.hex()})


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):                       # 安靜
        pass

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            body = open(TPL, "rb").read()            # 模板 CAP=null → live 模式
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path == "/stream":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            q = queue.Queue(maxsize=4096)
            with clients_lock:
                clients.append(q)
            try:
                self.wfile.write(("data: " + json.dumps(
                    {"meta": {"iface": self.server.iface}}) + "\n\n").encode())
                self.wfile.flush()
                last_meta = time.monotonic()
                while True:
                    try:
                        msg = q.get(timeout=1.0)
                        self.wfile.write(msg.encode())
                    except queue.Empty:
                        self.wfile.write(b": ka\n\n")     # SSE 保活註解
                    if time.monotonic() - last_meta > 2.0:
                        last_meta = time.monotonic()
                        self.wfile.write(("data: " + json.dumps(
                            {"meta": {"captured": stat["captured"],
                                      "cyclic_dropped": stat["captured"] - stat["sent"]}})
                            + "\n\n").encode())
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass
            finally:
                with clients_lock:
                    if q in clients:
                        clients.remove(q)
            return
        self.send_response(404)
        self.end_headers()


class Srv(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", required=True)
    ap.add_argument("--port", type=int, default=8792)
    a = ap.parse_args()
    threading.Thread(target=sniffer, args=(a.iface,), daemon=True).start()
    srv = Srv(("127.0.0.1", a.port), Handler)
    srv.iface = a.iface
    print("[live] http://localhost:%d  （iface=%s,Ctrl-C 結束）" % (a.port, a.iface))
    srv.serve_forever()


if __name__ == "__main__":
    main()
