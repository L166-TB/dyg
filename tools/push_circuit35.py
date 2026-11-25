# -*- coding: utf-8 -*-
"""把实验电路里的 2.8 寸 TFT 替换成 3.5 寸 TFT，并上传电路。

用法：
  python push_circuit35.py --project 85461 --url "<实验页URL>"
"""
import argparse
import hashlib
import json
import os
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import push_harmony as H  # noqa: E402

OUT = r"C:\Users\14393\Desktop\总文件夹\11\_circuit_35.json"
OLD_ID = "tftlcd"
NEW_ID = "tftlcd3_5"

NAME_MAP = {"DB%d" % i: "D%d" % i for i in range(16)}
NAME_MAP.update({"PEN": "INT", "T_CS": "TCS", "SDCS": "NC0", "NC": "BVDD",
                 "F_CS": "NC1", "VDD0": "VCC3_1", "VDD1": "VCC3_2"})


def cdn(url):
    req = urllib.request.Request(url, headers={
        "User-Agent": "Mozilla/5.0", "Referer": "https://app.puliedu.com/"})
    with H.P._no_proxy.open(req, timeout=60) as r:
        return r.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=int, default=85461)
    ap.add_argument("--url", default="")
    a = ap.parse_args()
    H.configure(a.project, a.url or None)
    H.open_session()
    try:
        detail = H.post("/api/lab/project/detail", {"project_id": H.PROJ})["data"]
        chip = detail["chip_id"]
        view_url = detail["view_url"]
        print("当前电路:", view_url)
        circuit = json.loads(cdn("https://cdn-obs.puliedu.com/app-prod/" + view_url).decode("utf-8"))

        comp = H.post("/api/resource/component/detail", {"component_id": NEW_ID})["data"]
        pins = json.loads(cdn("https://cdn-obs.puliedu.com/app-prod/" + comp["struct_url"]).decode("utf-8"))

        idx = next(i for i, n in enumerate(circuit["nodes"]) if n.get("name") == "tftlcd_1")
        node = circuit["nodes"][idx]
        if node.get("component_id") == NEW_ID:
            print("电路已经是 3.5 寸屏，无需替换")
            return
        old_pins = node["index"]
        scale = node["width"] / 1672.0
        node.update({
            "component_id": NEW_ID,
            "type": NEW_ID,
            "component_name": comp["component_name"],
            "cname": comp["component_name"],
            "img": comp["img_url"],
            "img_url": comp["img_url"],
            "so": comp["so"],
            "struct_url": comp["struct_url"],
            "doc_url": comp["doc_url"],
            "manuals": comp.get("manuals") or [],
            "index": pins,
            "width": round(2177 * scale, 2),
            "height": round(3201 * scale, 2),
        })
        print("替换节点:", node["name"], "引脚", len(old_pins), "->", len(pins),
              "尺寸 %.1fx%.1f" % (node["width"], node["height"]))

        moved = 0
        for w in circuit.get("allPointes") or []:
            for key, is_end in (("startNode", False), ("endNode", True)):
                q = w.get(key) or {}
                if q.get("nodeIndex") != idx:
                    continue
                old_port = q.get("portIndex")
                p = pins[old_port]
                q["portIndex"] = old_port
                q["text"] = "%s.%s" % (node["name"], p["name"])
                # 顶部显示屏引脚跟着新坐标走；触摸脚保持原折线位置
                if abs(q["y"] - (node["y"] + 8)) < 2:
                    q["x"] = node["x"] + p["x"] + 3
                    q["y"] = node["y"] + p["y"] + 3
                    pt = "%d,%d" % (q["x"], q["y"])
                    if w.get("points"):
                        if is_end:
                            w["points"][-1] = pt
                        else:
                            w["points"][0] = pt
                    moved += 1
        print("更新导线端点:", moved)

        raw = json.dumps(circuit, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        with open(OUT, "wb") as f:
            f.write(raw)
        print("电路已生成:", OUT, len(raw), "字节")

        sign = H.post("/api/common/upload/sign",
                      {"md5": hashlib.md5(raw).hexdigest() + ".json"})["data"]
        req = urllib.request.Request(
            sign["presigned_url"], data=raw, method="PUT",
            headers={"Content-Type": "application/json", "Content-Length": str(len(raw))})
        with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(req, timeout=180) as r:
            print("OBS 上传:", r.status)
        r = H.post("/api/lab/project/upload/view",
                   {"project_id": H.PROJ, "chip_id": chip, "view_url": sign["key"]})
        print("upload/view:", r.get("code"), r.get("cause", ""))
        now = H.post("/api/lab/project/detail", {"project_id": H.PROJ})["data"]["view_url"]
        print("新电路:", now)
        print("绑定成功" if now == sign["key"] else "绑定失败")
    finally:
        H.close_session()


if __name__ == "__main__":
    main()
