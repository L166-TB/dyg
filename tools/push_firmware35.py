# -*- coding: utf-8 -*-
"""把 3.5 寸屏 C 固件改动推到 puliedu 工程并编译。

用法：
  python push_firmware35.py --project 85461 --url "<实验页URL>"
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import push_harmony as H  # noqa: E402

LOCAL = r"C:\Users\14393\Desktop\总文件夹\11\_firmware35"
CHANGED = ["Makefile", "app/board_port.c", "app/cane_gui.c", "app/cane_runtime.c"]
NEW = ["app/tft35.c", "app/tft35.h"]
DROP = ["app/tft28.c", "app/tft28.h"]


def ensure_file(p):
    r = H.read_remote(p)
    if r.get("code") == 0:
        return
    for _ in range(4):
        r = H.manage(1, p, False)
        if r.get("code") == 0 or "已存在" in str(r.get("cause", "")):
            return
        if "exit status 255" not in str(r.get("cause", "")):
            break
        time.sleep(2)
    if r.get("code") != 0 and "已存在" not in str(r.get("cause", "")):
        raise RuntimeError("建文件失败 %s: %s" % (p, r))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=int, default=85461)
    ap.add_argument("--url", default="")
    a = ap.parse_args()
    H.configure(a.project, a.url or None)
    H.open_session()
    try:
        for rel in DROP:
            r = H.manage(2, H.BASE + rel, False)
            print("删除", rel, r.get("code"))
        for rel in NEW:
            ensure_file(H.BASE + rel)
        files = []
        for rel in NEW + CHANGED:
            with open(os.path.join(LOCAL, rel.replace("/", os.sep)), encoding="utf-8") as f:
                files.append({"file_path": H.BASE + rel, "contents": f.read()})
        r = H.post("/api/project/file/modify", {"project_id": H.PROJ, "files": files})
        if r.get("code") != 0:
            raise RuntimeError("写入失败: %s" % r)
        for item in r["data"].get("list", []):
            if item.get("is_success") != 1:
                raise RuntimeError("写入失败: %s" % item)
        print("写入", len(files), "个文件")
        H.post("/api/project/save", {"project_id": H.PROJ})

        entries = []
        for root, dnames, fnames in os.walk(LOCAL):
            for fn in fnames:
                full = os.path.join(root, fn)
                rel = os.path.relpath(full, LOCAL).replace("\\", "/")
                entries.append((rel, full))
        have = {r for r, _ in entries}
        for root, dnames, fnames in os.walk(H.LOCAL):
            dnames[:] = [d for d in dnames if d not in H.SKIP_DIRS]
            for fn in fnames:
                full = os.path.join(root, fn)
                rel = os.path.relpath(full, H.LOCAL).replace("\\", "/")
                if rel not in have:
                    entries.append((rel, full))
        H._upload_entries(entries)

        b = H.post("/api/project/build", {"project_id": H.PROJ}, timeout=300)
        d = b.get("data") or {}
        out = "".join((x.get("out_data") or "") for x in (d.get("build_result") or []))
        print("编译 status=%s error=%s warning=%s" %
              (d.get("build_status"), d.get("error_cnt"), d.get("warning_cnt")))
        if b.get("code") != 0 or d.get("error_cnt"):
            print(out[-3000:])
            raise RuntimeError("编译失败")

        # 把 build/main.hex 一并放进代码包，仿真端重载后需要它启动 CPU
        hr = H.read_remote(H.BASE + "build/main.hex")
        if hr.get("code") == 0:
            hx = hr["data"].get("source_code") or ""
            hp = os.path.join(LOCAL, "build", "main.hex")
            os.makedirs(os.path.dirname(hp), exist_ok=True)
            with open(hp, "w", encoding="utf-8", newline="") as f:
                f.write(hx)
            if not any(r == "build/main.hex" for r, _ in entries):
                entries.append(("build/main.hex", hp))
            H._upload_entries(entries)
            print("已把 build/main.hex 打入代码包", len(hx), "字节")
    finally:
        H.close_session()


if __name__ == "__main__":
    main()
