"""
ws_server.py — 假硬體 WebSocket 伺服器（純標準庫,無第三方相依）

把 phu_motor.py 的 14 顆真馬達（含完整 OD）跑成後端,前端 UI 透過 WebSocket：
  - 接收命令：enable / estop / jog / mode / read / write（OD 讀寫）
  - 廣播遙測：每顆馬達 cw/sw/狀態/目標/實際/扭矩/電流 + 最近 CAN 幀 + COB-ID 表

host_ui_ws.html 與 can_monitor_ws.html 連同一個 server → 看到同一份後端數據。

啟動： python3 ws_server.py [port]   （預設 8765）
"""
import socket, threading, time, json, base64, hashlib, struct, sys, collections
from phu_motor import PhuMotor, CPR
from phu_od import od_name

MAP = [("L_J1",0,1,"PHU20"),("L_J2",0,2,"PHU20"),("L_J3",0,3,"PHU17"),("L_J4",0,4,"PHU17"),
       ("L_J5",0,5,"PHU14"),("L_J6",0,6,"PHU14"),("L_J7",0,7,"PHU14"),
       ("R_J1",1,1,"PHU20"),("R_J2",1,2,"PHU20"),("R_J3",1,3,"PHU17"),("R_J4",1,4,"PHU17"),
       ("R_J5",1,5,"PHU14"),("R_J6",1,6,"PHU14"),("R_J7",1,7,"PHU14")]
STATE = {0x40:"SOD",0x21:"READY",0x23:"SWITCHED",0x27:"OP_ENABLED",0x07:"QSTOP",0x08:"FAULT"}
DT = 0.002

class Sim:
    def __init__(self):
        self.M = [PhuMotor(n, m, nm) for (nm,b,n,m) in MAP]
        self.bus = [b for (nm,b,n,m) in MAP]
        self.goal = [0,0.3,0,0.7,0,0.5,0, 0,0.3,0,0.7,0,0.5,0]
        self.cmd = [0.0]*14
        self.estop = False
        self.enseq = 0
        self.tx = [0,0]; self.rx = [0,0]
        self.cobid = {}                       # cobid -> dict
        self.frames = collections.deque(maxlen=30)
        self.lock = threading.Lock()

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
                    state=STATE.get(m.statusword,"?"), target=m.target_counts,
                    actual=int(round(m.q*CPR)), torque=round(m.torque,2), current=round(m.current,2),
                    peak=m.peak_torque, ratedC=m.rated_current))
            cobids=[dict(id="0x%03X"%k, **{kk:vv for kk,vv in v.items() if kk!="t"},
                         age=int((time.time()-v["t"])*1000)) for k,v in sorted(self.cobid.items())]
            return dict(motors=motors, frames=list(self.frames), cobids=cobids,
                        tx=self.tx, rx=self.rx, estop=self.estop)

sim = Sim()

# ---- 控制迴圈執行緒（~500Hz）----
def control_loop():
    while True:
        for _ in range(5): sim.step(DT)    # 5×2ms
        time.sleep(0.01)
threading.Thread(target=control_loop, daemon=True).start()

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

def main():
    port=int(sys.argv[1]) if len(sys.argv)>1 else 8765
    s=socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", port)); s.listen(8)
    print("假硬體 WebSocket 伺服器啟動：ws://localhost:%d  (Ctrl+C 結束)"%port)
    while True:
        conn,_=s.accept()
        threading.Thread(target=handle_client, args=(conn,), daemon=True).start()

if __name__=="__main__":
    main()
