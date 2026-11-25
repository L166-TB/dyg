# -*- coding: utf-8 -*-
"""给 3.5 寸屏补 NC1->GND1、BVDD->VCC3_1 两根线，消除未绑定警告并给背光升压供电。"""
import hashlib
import json
import os
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import push_harmony as H  # noqa: E402

URL = "https://app.puliedu.com/#/simulation?__params=JTdCJTIycHJvamVjdF9pZCUyMiUzQSUyMjg1NDYxJTIyJTJDJTIybGFiX2lkJTIyJTNBJTIyNTI1NDMlMjIlMkMlMjJsYWJfbmFtZSUyMiUzQSUyMjExJTIyJTdE"
JSON_PATH = r"C:\Users\14393\Desktop\总文件夹\11\_circuit_35.json"


def cdn(url):
    req = urllib.request.Request(url, headers={
        "User-Agent": "Mozilla/5.0", "Referer": "https://app.puliedu.com/"})
    with H.P._no_proxy.open(req, timeout=60) as r:
        return r.read()


def main():
    H.configure(85461, URL)
    H.open_session()
    try:
        detail = H.post("/api/lab/project/detail", {"project_id": H.PROJ})["data"]
        circuit = json.loads(cdn("https://cdn-obs.puliedu.com/app-prod/" + detail["view_url"]).decode())
        idx = next(i for i, n in enumerate(circuit["nodes"]) if n.get("name") == "tftlcd_1")
        node = circuit["nodes"][idx]
        pins = node["index"]

        def ep(port):
            p = pins[port]
            return {"name": "pin", "nodeIndex": idx, "portIndex": port,
                    "text": "tftlcd_1." + p["name"],
                    "x": node["x"] + p["x"] + 3, "y": node["y"] + p["y"] + 3}

        pairs = [(31, 25), (27, 23)]  # NC1->GND1, BVDD->VCC3_1
        wires = circuit.get("allPointes") or []
        have = {frozenset([(w.get("startNode") or {}).get("text"), (w.get("endNode") or {}).get("text")])
                for w in wires}
        for a, b in pairs:
            ea, eb = ep(a), ep(b)
            key = frozenset([ea["text"], eb["text"]])
            if key in have:
                print("已存在", ea["text"], eb["text"])
                continue
            wires.append({
                "startNode": ea, "endNode": eb,
                "points": ["%d,%d" % (ea["x"], ea["y"]), "%d,%d" % (eb["x"], eb["y"])],
                "stroke": "#519596", "strokeWidth": 2,
            })
            print("新增", ea["text"], "->", eb["text"])
        circuit["allPointes"] = wires

        raw = json.dumps(circuit, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        open(JSON_PATH, "wb").write(raw)
        sign = H.post("/api/common/upload/sign",
                      {"md5": hashlib.md5(raw).hexdigest() + ".json"})["data"]
        req = urllib.request.Request(
            sign["presigned_url"], data=raw, method="PUT",
            headers={"Content-Type": "application/json", "Content-Length": str(len(raw))})
        with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(req, timeout=180) as r:
            print("OBS", r.status)
        r = H.post("/api/lab/project/upload/view",
                   {"project_id": H.PROJ, "chip_id": detail["chip_id"], "view_url": sign["key"]})
        print("upload/view", r.get("code"), r.get("cause", ""))
        print("新电路", sign["key"])
    finally:
        H.close_session()


if __name__ == "__main__":
    main()
