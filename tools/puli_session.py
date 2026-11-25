#!/usr/bin/env python3
"""
puliedu 会话脚本 —— 启动浏览器 + 执行任意操作，全程同一进程内完成。

用法：
  python puli_session.py list [元件名]
  python puli_session.py verify --expect A=B --project 54639
  python puli_session.py eval "<JS表达式>"
  python puli_session.py connect --from X.P --to Y.Q [--replace] [--save] [--dry-run]

之所以要「一体化」，是因为本环境每次工具调用都是独立进程，
浏览器子进程会随父进程退出而关闭，无法跨调用复用 CDP 连接。
"""
import base64
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request

EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
SRC_USER_DATA = os.path.join(os.environ["LOCALAPPDATA"], "Microsoft", "Edge", "User Data")
SKIP_NAMES = {"LOCK", "SingletonLock", "SingletonCookie", "SingletonSocket"}
PORT = int(os.environ.get("PULI_PORT", "9222"))
PROFILE = os.path.join(tempfile.gettempdir(), "puliedu-session-profile")

# 默认实验：鸿蒙（project_id=54639 / lab_id=35676，GD32VF103V 智慧农业环境监测系统）
DEFAULT_URL = ("https://app.puliedu.com/#/simulation?__params="
               "JTdCJTIycHJvamVjdF9pZCUyMiUzQSUyMjU0NjM5JTIyJTJDJTIybGFiX2lkJTIyJTNBJTIyMzU2NzYlMjIlMkMlMjJsYWJfbmFtZSUyMiUzQSUyMiVFOSVCOCVCRiVFOCU5MiU5OSUyMiU3RA==")

_no_proxy = urllib.request.build_opener(urllib.request.ProxyHandler({}))

# ───────────────────────── CONNECT_JS（Python 原生字符串版） ─────────────────────────
# 原版在 puliedu_circuit_cli.js 里是 Node 模板字面量，含 ${...} 插值。
# 从 Python 侧送进浏览器会因 ${} 造成语法错误；这里整体改写成 Python 字符串，
# 只在 6 个明确的占位符处替换，不再用正则兜底（正则兜底会误伤正文里的 return）。
CONNECT_JS = r"""(async () => {
  const c = window.__PULI_CIRC;
  const F = __FROM__, T = __TO__;
  const REPLACE = __REPLACE__, DRY = __DRY__, SAVE = __SAVE__, FORCE = __FORCE__;
  const STUB = __STUB__;
  const PROJECT_ID = __PROJECT__;

  const findPin = (text) => {
    const i = text.indexOf('.');
    if (i < 0) return null;
    const inst = text.slice(0, i), pin = text.slice(i + 1);
    const ni = c.nodes.findIndex((n) => n.name === inst || n.aliasName === inst);
    if (ni < 0) return null;
    const node = c.nodes[ni];
    const pi = (node.index || []).findIndex((p) => p.name === pin);
    if (pi < 0) return null;
    return { nodeIndex: ni, portIndex: pi, node, port: node.index[pi] };
  };
  const offsetOf = (nodeIndex) => {
    const node = c.nodes[nodeIndex];
    for (const w of c.allPointes) {
      for (const ep of [w.startNode, w.endNode]) {
        if (!ep || ep.nodeIndex !== nodeIndex || typeof ep.portIndex !== 'number') continue;
        if (!ep.text || !ep.text.startsWith(node.name + '.')) continue;
        const p = (node.index || [])[ep.portIndex];
        if (p) return { dx: ep.x - (node.x + p.x), dy: ep.y - (node.y + p.y) };
      }
    }
    const d = node.kindof === 'cpu' ? 2 : 3;
    return { dx: d, dy: d };
  };
  const abs = (pin) => {
    const o = offsetOf(pin.nodeIndex);
    return { x: pin.node.x + pin.port.x + o.dx, y: pin.node.y + pin.port.y + o.dy, dir: pin.port.direction || 'bottom' };
  };
  const A = findPin(F), B = findPin(T);
  if (!A) return JSON.stringify({ ok: false, error: 'pin_not_found', pin: F });
  if (!B) return JSON.stringify({ ok: false, error: 'pin_not_found', pin: T });
  const p1 = abs(A), p2 = abs(B);

  const V = { bottom: [0, 1], top: [0, -1], left: [-1, 0], right: [1, 0] };
  const route = (a, b) => {
    const v1 = V[a.dir] || [0, 1], v2 = V[b.dir] || [0, 1];
    const s1 = { x: a.x + v1[0] * STUB, y: a.y + v1[1] * STUB };
    const s2 = { x: b.x + v2[0] * STUB, y: b.y + v2[1] * STUB };
    const pts = [{ x: a.x, y: a.y }, s1];
    const vert1 = v1[0] === 0, vert2 = v2[0] === 0;
    const laneOf = (c1, c2, d1, d2) => (d1 === d2 ? (d1 > 0 ? Math.max(c1, c2) : Math.min(c1, c2)) : Math.round((c1 + c2) / 2));
    if (vert1 && vert2) { const lane = laneOf(s1.y, s2.y, v1[1], v2[1]); pts.push({ x: s1.x, y: lane }, { x: s2.x, y: lane }, s2); }
    else if (!vert1 && !vert2) { const lane = laneOf(s1.x, s2.x, v1[0], v2[0]); pts.push({ x: lane, y: s1.y }, { x: lane, y: s2.y }, s2); }
    else if (vert1) pts.push({ x: s1.x, y: s2.y }, s2);
    else pts.push({ x: s2.x, y: s1.y }, s2);
    pts.push({ x: b.x, y: b.y });
    const out = [];
    for (const p of pts) { const l = out[out.length - 1]; if (!l || l.x !== p.x || l.y !== p.y) out.push(p); }
    return out;
  };
  const mkEnd = (pin, p) => ({ nodeIndex: pin.nodeIndex, portIndex: pin.portIndex, x: p.x, y: p.y, name: 'pin', text: pin.node.name + '.' + pin.port.name });
  const wire = { points: route(p1, p2).map((p) => p.x + ',' + p.y), startNode: mkEnd(A, p1), endNode: mkEnd(B, p2), stroke: '#519596', strokeWidth: 2 };

  const touching = c.allPointes
    .map((w, i) => ({ w, i }))
    .filter(({ w }) => [w.startNode, w.endNode].some((e) => e && e.text === F));
  const same = touching.find(({ w }) => [w.startNode, w.endNode].some((e) => e && e.text === T));
  if (same && !FORCE) {
    return JSON.stringify({ ok: true, action: 'already_connected', wire: same.w, index: same.i, saved: false });
  }
  if (DRY) return JSON.stringify({ ok: true, action: 'dry_run', wire, wouldReplace: REPLACE ? touching.map((t) => t.i) : [] });

  let action, index = -1;
  if (touching.length) {
    if (!REPLACE) return JSON.stringify({ ok: false, error: 'pin_busy', message: F + ' 已经接了别的线，加 --replace 改接', existing: touching.map((t) => ((t.w.startNode.text === F ? t.w.endNode : t.w.startNode) || {}).text) });
    index = touching[0].i;
    c.$set(c.allPointes, index, wire);
    action = 'replaced';
  } else {
    c.allPointes.push(wire);
    index = c.allPointes.length - 1;
    action = 'added';
  }
  if (c.checkCanvasChanged) c.checkCanvasChanged();
  c.$forceUpdate();

  let saved = false, viewUrl = null, saveError = null;
  if (SAVE) {
    try {
      const store = document.querySelector('#app').__vue__.$store;
      await Promise.resolve(c.saveCircuit(false));
      for (let k = 0; k < 40; k++) {
        const changed = store.state.newCircuit.hasCanvasChanged;
        if (!changed) break;
        await new Promise((r) => setTimeout(r, 250));
      }
      saved = !store.state.newCircuit.hasCanvasChanged;
      const r = await fetch('/api/lab/project/detail', {
        method: 'POST', headers: { 'Content-Type': 'application/json', Authorization: localStorage.getItem('token') },
        body: JSON.stringify({ project_id: PROJECT_ID }),
      }).then((x) => x.json());
      viewUrl = r && r.data && r.data.view_url;
    } catch (e) { saveError = String(e && e.message || e); }
  }
  return JSON.stringify({ ok: true, action, index, wire, saved, viewUrl, saveError });
})()"""


def build_connect_js(frm, to, flags, project=54639):
    """把 CONNECT_JS 的 6 个占位符替换成字面量。"""
    js = CONNECT_JS
    js = js.replace("__FROM__", json.dumps(frm))
    js = js.replace("__TO__", json.dumps(to))
    js = js.replace("__REPLACE__", "true" if flags.get("replace") else "false")
    js = js.replace("__DRY__", "true" if flags.get("dry-run") else "false")
    js = js.replace("__SAVE__", "true" if flags.get("save") else "false")
    js = js.replace("__FORCE__", "true" if flags.get("force") else "false")
    js = js.replace("__STUB__", str(int(flags.get("stub", 20))))
    js = js.replace("__PROJECT__", str(int(project)))
    return js


class WS:
    def __init__(self, url, timeout=30):
        rest = url[5:]
        hostport, _, path = rest.partition("/")
        host, _, port = hostport.partition(":")
        self.sock = socket.create_connection((host, int(port or 80)), timeout=timeout)
        self.sock.settimeout(timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((
            f"GET /{path} HTTP/1.1\r\nHost: {hostport}\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            c = self.sock.recv(4096)
            if not c:
                raise RuntimeError("握手失败")
            buf += c
        self.buf = buf.split(b"\r\n\r\n", 1)[1]
        self._id = 0

    def _exact(self, n):
        while len(self.buf) < n:
            c = self.sock.recv(65536)
            if not c:
                raise RuntimeError("连接关闭")
            self.buf += c
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def send(self, text):
        pl = text.encode(); mask = os.urandom(4); n = len(pl)
        h = bytearray([0x81])
        if n < 126:
            h.append(0x80 | n)
        elif n < 65536:
            h.append(0x80 | 126); h += struct.pack(">H", n)
        else:
            h.append(0x80 | 127); h += struct.pack(">Q", n)
        h += mask
        self.sock.sendall(bytes(h) + bytes(b ^ mask[i % 4] for i, b in enumerate(pl)))

    def recv(self):
        while True:
            op = self._exact(1)[0] & 0x0F
            b1 = self._exact(1)[0]; ln = b1 & 0x7F
            if ln == 126:
                ln = struct.unpack(">H", self._exact(2))[0]
            elif ln == 127:
                ln = struct.unpack(">Q", self._exact(8))[0]
            data = self._exact(ln)
            if op == 0x1:
                return data.decode("utf-8", "replace")
            if op == 0x8:
                raise RuntimeError("WS 关闭")

    def call(self, method, params=None):
        self._id += 1; mid = self._id
        self.send(json.dumps({"id": mid, "method": method, "params": params or {}}))
        end = time.time() + 60
        while time.time() < end:
            m = json.loads(self.recv())
            if m.get("id") == mid:
                if "error" in m:
                    raise RuntimeError(str(m["error"]))
                return m.get("result", {})
        raise TimeoutError(method)

    def ev(self, expr, await_promise=False):
        r = self.call("Runtime.evaluate", {"expression": expr, "returnByValue": True,
                                           "awaitPromise": await_promise})
        if r.get("exceptionDetails"):
            d = r["exceptionDetails"]
            raise RuntimeError(d.get("exception", {}).get("description") or d.get("text"))
        return r.get("result", {}).get("value")

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def cdp(path, timeout=8):
    with _no_proxy.open("http://127.0.0.1:%d%s" % (PORT, path), timeout=timeout) as f:
        return json.loads(f.read())


def copy_profile(src_root, dst_root):
    os.makedirs(dst_root, exist_ok=True)
    n = 0

    def one(sp, dp):
        nonlocal n
        try:
            if os.path.isdir(sp):
                shutil.copytree(sp, dp, dirs_exist_ok=True)
            else:
                os.makedirs(os.path.dirname(dp), exist_ok=True)
                shutil.copy2(sp, dp)
            n += 1
        except Exception:
            try:
                with open(sp, "rb") as fi, open(dp, "wb") as fo:
                    fo.write(fi.read())
                n += 1
            except Exception:
                pass

    def walk(src, dst):
        if not os.path.exists(src):
            return
        if os.path.isfile(src):
            one(src, dst)
            return
        os.makedirs(dst, exist_ok=True)
        for name in os.listdir(src):
            if name in SKIP_NAMES:
                continue
            one(os.path.join(src, name), os.path.join(dst, name))

    for rel in [os.path.join("Default", "Local Storage"),
                os.path.join("Default", "Session Storage"),
                os.path.join("Default", "IndexedDB"),
                os.path.join("Default", "Network"),
                "Local State",
                os.path.join("Default", "Preferences"),
                os.path.join("Default", "Secure Preferences")]:
        walk(os.path.join(src_root, rel), os.path.join(dst_root, rel))
    return n


def start_browser(url, reuse=False):
    """启动 CDP 浏览器。返回 (proc, profile)。"""
    profile = PROFILE
    if not reuse or not os.path.isdir(profile):
        copy_profile(SRC_USER_DATA, profile)
    for name in SKIP_NAMES:
        for sub in ["", "Default"]:
            p = os.path.join(profile, sub, name)
            if os.path.exists(p):
                try:
                    os.remove(p)
                except Exception:
                    pass
    env = dict(os.environ)
    for k in ["HTTP_PROXY", "HTTPS_PROXY", "http_proxy", "https_proxy",
              "ALL_PROXY", "all_proxy"]:
        env.pop(k, None)
    env["NO_PROXY"] = env["no_proxy"] = "localhost,127.0.0.1,::1"
    proc = subprocess.Popen(
        [EDGE, "--remote-debugging-port=%d" % PORT, "--remote-allow-origins=*",
         "--user-data-dir=" + profile, "--no-first-run", "--no-default-browser-check",
         "--no-proxy-server", "--window-size=1680,1000", url],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(40):
        time.sleep(1)
        try:
            cdp("/json/version", timeout=3)
            return proc, profile
        except Exception:
            pass
    raise RuntimeError("CDP 未就绪")


def get_page():
    tabs = [t for t in cdp("/json/list") if t.get("type") == "page"]
    pages = [t for t in tabs if "puliedu.com" in (t.get("url") or "")]
    if not pages:
        raise RuntimeError("没有 puliedu 标签页")
    return pages[-1]


FIND_JS = """(()=>{
  const el=document.querySelector('#app'); const vm=el&&el.__vue__;
  if(!vm) return {error:'no_vue_app'};
  let circ=null;
  (function w(c,d){ if(circ||!c||d>16) return;
    const dt=c._data;
    if(dt&&Array.isArray(dt.allPointes)&&Array.isArray(dt.nodes)){circ=c;return;}
    (c.$children||[]).forEach(k=>w(k,d+1)); })(vm,0);
  if(!circ) return {error:'circuit_not_loaded'};
  window.__PULI_CIRC=circ;
  return {ok:true,nodes:circ.nodes.length,wires:circ.allPointes.length};
})()"""


def wait_circuit(ws, timeout=90):
    """轮询等待画布组件就绪（128 元件的实验加载较慢）。"""
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            r = ws.ev(FIND_JS)
        except Exception as e:
            last = {"error": str(e)}
            time.sleep(1.5)
            continue
        last = r
        if r and r.get("ok"):
            return r
        time.sleep(1.5)
    return last


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    action = sys.argv[1]
    rest = sys.argv[2:]

    # 解析 --key value
    opt = {}
    i = 0
    while i < len(rest):
        a = rest[i]
        if a.startswith("--"):
            k = a[2:]
            nxt = rest[i + 1] if i + 1 < len(rest) else None
            if nxt is None or nxt.startswith("--"):
                opt[k] = True
            else:
                opt[k] = nxt; i += 1
        else:
            opt.setdefault("_pos", []).append(a)
        i += 1

    # 默认打开「鸿蒙」实验（project_id=54639 / lab_id=35676）
    url = opt.get("url") or DEFAULT_URL

    print("[*] 启动浏览器…")
    proc, profile = start_browser(url)
    print("[*] CDP 就绪 :%d  profile=%s" % (PORT, profile))

    try:
        page = get_page()
        ws = WS(page["webSocketDebuggerUrl"])
        ws.call("Runtime.enable")
        print("[*] 已连接标签页:", (page.get("url") or "")[:90])
        print("[*] 等待仿真画布就绪…")
        w = wait_circuit(ws)
        if w and w.get("ok"):
            print("[*] 画布就绪 ✓ 元件=%d 导线=%d" % (w["nodes"], w["wires"]))
        else:
            print("[*] 画布状态:", w)
    except Exception as e:
        print("[!] 连接失败:", e)
        proc.terminate()
        return 1

    rc = 0
    try:
        if action == "list":
            r = ws.ev(FIND_JS)
            if not r or r.get("error"):
                print("[!] 画布未就绪:", r); rc = 1
            else:
                comp = opt.get("_pos", [None])[0]
                js = """(()=>{
                  const c=window.__PULI_CIRC;
                  const wires=c.allPointes.map(w=>({from:(w.startNode&&w.startNode.text)||'?',
                                                    to:(w.endNode&&w.endNode.text)||'?'}));
                  const nodes=c.nodes.map((n,i)=>({i,name:n.name,cid:n.component_id,kindof:n.kindof,
                    x:n.x,y:n.y,pins:(n.index||[]).map((p,pi)=>({pi,name:p.name,dir:p.direction}))}));
                  const one=%s;
                  return JSON.stringify({
                    nodes: one? nodes.filter(n=>n.name===one||n.cid===one) : nodes.map(n=>({name:n.name,cid:n.cid,kindof:n.kindof})),
                    wires: one? wires.filter(w=>w.from.startsWith(one+'.')||w.to.startsWith(one+'.')) : wires
                  });
                })()""" % json.dumps(comp)
                d = json.loads(ws.ev(js))
                if comp:
                    for n in d["nodes"]:
                        print("元件 %s (%s, %s)  x=%s y=%s" % (n["name"], n["cid"], n["kindof"], n["x"], n["y"]))
                        for p in n["pins"]:
                            print("   [%d] %-10s %s" % (p["pi"], p["name"], p["dir"] or ""))
                else:
                    names = [n["name"] for n in d["nodes"]]
                    print("元件(%d): %s" % (len(names), ", ".join(names)))
                print("接线(%d):" % len(d["wires"]))
                for w in d["wires"]:
                    print("   %s  ->  %s" % (w["from"], w["to"]))

        elif action == "eval":
            expr = opt.get("_pos", [None])[0]
            if not expr:
                print("用法: eval \"<JS>\""); rc = 2
            else:
                print(json.dumps(ws.ev(expr, True), ensure_ascii=False, indent=2))

        elif action == "verify":
            expect = opt.get("expect")
            proj = int(opt.get("project", 54639))
            token = opt.get("token") or ws.ev("localStorage.getItem('token')||''")
            auth = token if token.startswith("Bearer") else "Bearer " + token
            a, b = (expect or "=").split("=")
            js = """(()=>fetch('/api/lab/project/detail',{method:'POST',
              headers:{'Content-Type':'application/json','Authorization':localStorage.getItem('token')},
              body:JSON.stringify({project_id:%d})}).then(r=>r.json())
              .then(j=>JSON.stringify({code:j.code,view_url:j.data&&j.data.view_url}))
              .catch(e=>JSON.stringify({error:String(e)})))()""" % proj
            det = json.loads(ws.ev(js, True))
            if det.get("code") != 0:
                print("[!] detail 失败:", det); rc = 1
            else:
                vu = det["view_url"]
                print("view_url:", vu)
                js2 = """(()=>fetch('https://cdn-obs.puliedu.com/app-prod/%s?t='+Date.now())
                  .then(r=>r.json()).then(c=>{
                    const wires=c.allPointes.map(w=>[(w.startNode&&w.startNode.text)||'?',
                                                     (w.endNode&&w.endNode.text)||'?']);
                    const hit=wires.find(([x,y])=>(x===%s&&y===%s)||(x===%s&&y===%s));
                    return JSON.stringify({nodes:c.nodes.length,wires:wires.length,found:!!hit,
                      related:wires.filter(([x,y])=>x.startsWith(%s)||y.startsWith(%s)).slice(0,25)});
                  }).catch(e=>JSON.stringify({error:String(e)})))()""" % (
                    vu, json.dumps(a), json.dumps(b), json.dumps(b), json.dumps(a),
                    json.dumps(a.split(".")[0] + "."), json.dumps(a.split(".")[0] + "."))
                d = json.loads(ws.ev(js2, True))
                print("元件=%s 导线=%s" % (d.get("nodes"), d.get("wires")))
                print("相关接线:")
                for x, y in d.get("related", []):
                    print("   %s -> %s" % (x, y))
                if d.get("found"):
                    print("RESULT: PASS — %s <-> %s" % (a, b))
                else:
                    print("RESULT: FAIL — 未找到 %s <-> %s" % (a, b)); rc = 1

        elif action == "connect":
            frm, to = opt.get("from"), opt.get("to")
            if not frm or not to:
                print("用法: connect --from X.P --to Y.Q [--replace] [--save] [--dry-run]"); rc = 2
            else:
                r = ws.ev(FIND_JS)
                if not r or r.get("error"):
                    print("[!] 画布未就绪:", r); rc = 1
                else:
                    flags = {k: opt[k] for k in ["replace", "dry-run", "save", "force"] if k in opt}
                    if "stub" in opt:
                        flags["stub"] = opt["stub"]
                    proj = int(opt.get("project", 54639))
                    expr = build_connect_js(frm, to, flags, proj)
                    out = ws.ev(expr, True)
                    print(json.dumps(json.loads(out) if isinstance(out, str) else out,
                                     ensure_ascii=False, indent=2))

        else:
            print("未知操作:", action); rc = 2
    except Exception as e:
        print("[!] 执行出错:", type(e).__name__, e); rc = 1
    finally:
        ws.close()
        proc.terminate()
        print("[*] 浏览器已关闭")
    return rc


if __name__ == "__main__":
    sys.exit(main())
