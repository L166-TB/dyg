# -*- coding: utf-8 -*-
"""把本地鸿蒙工程同步到 puliedu 在线工作区，并清理不再使用的旧代码文件。

复用本工具箱的 puli_session（CDP 会话 / 登录态 / 免代理通道）和平台 API：
  projectDir / projectFileManage / projectFileModify / projectFileOpen / projectSave

用法：
  python push_harmony.py probe     # 只看线上目录和本地待同步清单
  python push_harmony.py backup    # 只备份线上旧代码到本地
  python push_harmony.py push      # 建目录 + 建文件 + 写入 + 保存
  python push_harmony.py cleanup   # 删除线上不在本地清单里的旧文件/目录
  python push_harmony.py verify    # 校验线上文件与本地内容一致
  python push_harmony.py upload    # 打包整个鸿蒙工程并上传到平台代码包（持久化）
  python push_harmony.py all       # backup -> push -> cleanup -> upload -> verify
"""
import hashlib
import io
import json
import os
import posixpath
import sys
import time
import urllib.request
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import puli_session as P  # noqa: E402

URL = ("https://app.puliedu.com/#/simulation?__params="
       "JTdCJTIycHJvamVjdF9pZCUyMiUzQSUyMjYwMjc1JTIyJTJDJTIybGFiX2lkJTIyJTNBJTIyMzk0ODklMjIlMkMlMjJsYWJfbmFtZSUyMiUzQSUyMiVFOCU5NCU5NyVFNyVCMyU5NiVFNSU4OCVCNiVFNCVCRCU5QyVFNyVCMyVCQiVFNyVCQiU5RiVFMiU4MCU5NCVFMiU4MCU5NCVFNyVCQiU4OCVFNyU4OSU4OCUyMiUyQyUyMmxhYl90eXBlJTIyJTNBJTIyMiUyMiU3RA==")
PROJ = 60275
BASE = "/opt/lab/workspace/60254/60275/"
LOCAL = r"C:\Users\14393\Desktop\总文件夹\11\_harmony\CaneFlowHarmony"
BACKUP = r"C:\Users\14393\Desktop\总文件夹\11\_harmony\backup_60275_old"
RESTORE = r"C:\Users\14393\Desktop\总文件夹\11\_harmony\backup_60275_old\20261124_215532"
SKIP_DIRS = {".git", "node_modules", "oh_modules", ".hvigor"}
SKIP_EXT = {".png", ".jpg", ".jpeg", ".gif", ".ico", ".zip", ".class", ".jar"}

_session = None


def configure(project=None, url=None, base=None, local=None):
    """切换目标工程；base 留空时由 open_session 从线上目录接口自动解析。"""
    global PROJ, URL, BASE, LOCAL, BACKUP
    if local:
        LOCAL = local
    if project:
        PROJ = int(project)
        BACKUP = os.path.join(os.path.dirname(LOCAL), "backup_%d_old" % PROJ)
    if url:
        URL = url
    if base:
        BASE = base.rstrip("/") + "/"


def post(path, body, tok=None, timeout=90, retry=4):
    if tok is None:
        tok = _session["token"]
    data = json.dumps(body).encode("utf-8")
    last = None
    for i in range(retry):
        req = urllib.request.Request(
            "https://app.puliedu.com" + path, data=data,
            headers={"Content-Type": "application/json", "Authorization": tok,
                     "User-Agent": "Mozilla/5.0", "Origin": "https://app.puliedu.com"},
            method="POST")
        try:
            with P._no_proxy.open(req, timeout=timeout) as r:
                return json.loads(r.read().decode("utf-8", "replace"))
        except Exception as e:
            last = e
            time.sleep(1.5)
    return {"code": -1, "cause": str(last)[:300]}


def open_session():
    global _session, BASE
    if _session:
        return _session
    proc, profile = P.start_browser(URL)
    page = P.get_page()
    ws = P.WS(page["webSocketDebuggerUrl"])
    ws.call("Runtime.enable")
    for _ in range(60):
        if ws.ev("!!(document.querySelector('#app')&&document.querySelector('#app').__vue__)"):
            break
        time.sleep(1)
    time.sleep(3)
    tok = ws.ev("localStorage.getItem('token')||''") or ""
    if not tok:
        raise RuntimeError("未取到登录 token")
    _session = {"proc": proc, "ws": ws, "token": tok}
    # 以线上目录接口返回的根路径为准，避免写死用户号/工程号
    d = post("/api/project/directory", {"project_id": PROJ, "_t": int(time.time() * 1000)})
    if d.get("code") == 0 and (d.get("data") or {}).get("path"):
        BASE = d["data"]["path"].rstrip("/") + "/"
    return _session


def close_session():
    global _session
    if not _session:
        return
    try:
        _session["ws"].close()
    except Exception:
        pass
    try:
        _session["proc"].terminate()
    except Exception:
        pass
    _session = None


def tree():
    d = post("/api/project/directory", {"project_id": PROJ, "_t": int(time.time() * 1000)})
    if d.get("code") != 0:
        raise RuntimeError("目录读取失败: %s" % d)
    return d["data"]


def flatten(node, files=None, dirs=None):
    files = files if files is not None else {}
    dirs = dirs if dirs is not None else {}
    if not isinstance(node, dict):
        return files, dirs
    p = node.get("path", "")
    if p:
        dirs[p.rstrip("/")] = node
    for c in node.get("children") or []:
        cp = c.get("path", "")
        if c.get("is_dir"):
            flatten(c, files, dirs)
        elif cp:
            files[cp] = c
    return files, dirs


def local_manifest():
    files, dirs = {}, set()
    for root, dnames, fnames in os.walk(LOCAL):
        dnames[:] = [d for d in dnames if d not in SKIP_DIRS]
        rel_dir = os.path.relpath(root, LOCAL).replace("\\", "/")
        if rel_dir != ".":
            dirs.add(BASE + rel_dir)
        for fn in fnames:
            if os.path.splitext(fn)[1].lower() in SKIP_EXT:
                print("  [跳过二进制]", rel_dir + "/" + fn)
                continue
            full = os.path.join(root, fn)
            rel = (rel_dir + "/" + fn) if rel_dir != "." else fn
            files[BASE + rel] = full
    return files, dirs


def manage(ftype, path, is_dir=False, new_path=""):
    return post("/api/project/file/manage", {
        "project_id": PROJ, "type": ftype, "file_path": path,
        "is_dir": bool(is_dir), "new_file_path": new_path})


def read_remote(path):
    return post("/api/project/file/open", {"project_id": PROJ, "file_path": path})


def do_probe():
    open_session()
    rf, rd = flatten(tree())
    lf, ld = local_manifest()
    print("线上文件 %d / 目录 %d；本地待同步 %d / 目录 %d" %
          (len(rf), len(rd) - 1, len(lf), len(ld)))
    print("线上旧文件（将被清理）:")
    for p in sorted(set(rf) - set(lf)):
        print("  -", p[len(BASE):])
    print("本地待新增文件:")
    for p in sorted(set(lf) - set(rf)):
        print("  +", p[len(BASE):])


def do_backup():
    open_session()
    rf, _ = flatten(tree())
    stamp = time.strftime("%Y%m%d_%H%M%S")
    dest = os.path.join(BACKUP, stamp)
    n = 0
    for p, meta in sorted(rf.items()):
        r = read_remote(p)
        if r.get("code") != 0:
            print("  [读取失败]", p)
            continue
        rel = p[len(BASE):].replace("/", os.sep)
        out = os.path.join(dest, rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "w", encoding="utf-8", newline="") as f:
            f.write(r["data"].get("source_code") or "")
        n += 1
    print("已备份 %d 个文件 -> %s" % (n, dest))


def do_push():
    open_session()
    lf, ld = local_manifest()
    rf, rd = flatten(tree())

    for p in sorted(ld, key=lambda x: x.count("/")):
        if p not in rd:
            r = manage(1, p, True)
            if r.get("code") != 0:
                raise RuntimeError("建目录失败 %s: %s" % (p, r))
    print("目录就绪: %d" % len(ld))

    created = 0
    for p in sorted(lf):
        if p in rf:
            continue
        # 目录接口不列出隐藏文件，先通过 open 接口确认是否已存在
        if os.path.basename(p).startswith(".") and read_remote(p).get("code") == 0:
            continue
        r = None
        for attempt in range(4):
            r = manage(1, p, False)
            cause = str(r.get("cause", ""))
            if r.get("code") == 0 or "已存在" in cause:
                break
            # 服务端偶发 exit status 255（实际已建出空文件），重试即可
            if "exit status 255" not in cause:
                break
            time.sleep(2)
        if r.get("code") != 0 and "已存在" not in str(r.get("cause", "")):
            raise RuntimeError("建文件失败 %s: %s" % (p, r))
        created += 1
    print("新建文件: %d" % created)

    batch, size, done = [], 0, 0
    for p, local_path in sorted(lf.items()):
        with open(local_path, "r", encoding="utf-8") as f:
            content = f.read()
        batch.append({"file_path": p, "contents": content})
        size += len(content.encode("utf-8"))
        if size >= 400000 or len(batch) >= 30:
            _write_batch(batch); done += len(batch); batch, size = [], 0
    if batch:
        _write_batch(batch); done += len(batch)
    print("写入文件: %d" % done)

    r = post("/api/project/save", {"project_id": PROJ})
    if r.get("code") != 0:
        raise RuntimeError("保存失败: %s" % r)
    print("已保存")


def _write_batch(batch):
    r = post("/api/project/file/modify", {"project_id": PROJ, "files": batch})
    if r.get("code") != 0:
        raise RuntimeError("写入失败: %s" % r)
    for item in r["data"].get("list", []):
        if item.get("is_success") != 1:
            raise RuntimeError("写入失败: %s" % item)
    print("  写入 %d 个文件" % len(batch))


def do_verify(strict=True):
    open_session()
    lf, ld = local_manifest()
    rf, rd = flatten(tree())
    # 目录接口不返回以点开头的隐藏文件，隐藏文件用 open 接口单独校验
    missing = sorted(p for p in set(lf) - set(rf)
                     if not os.path.basename(p).startswith("."))
    # 二进制资源随代码包上传，但不参与文本比对
    extra_all = sorted(set(rf) - set(lf))
    extra = sorted(p for p in extra_all
                   if os.path.splitext(p)[1].lower() not in SKIP_EXT)
    binaries = [p for p in extra_all if p not in extra]
    print("线上缺失 %d / 线上多余 %d / 二进制资源 %d" %
          (len(missing), len(extra), len(binaries)))
    for p in missing[:20]:
        print("  [缺失]", p[len(BASE):])
    if strict:
        for p in extra[:20]:
            print("  [多余]", p[len(BASE):])
    bad = 0
    for p, local_path in sorted(lf.items()):
        r = read_remote(p)
        if r.get("code") != 0:
            print("  [读取失败]", p[len(BASE):]); bad += 1; continue
        with open(local_path, "r", encoding="utf-8") as f:
            local_txt = f.read().replace("\r\n", "\n")
        remote_txt = (r["data"].get("source_code") or "").replace("\r\n", "\n")
        if local_txt != remote_txt:
            print("  [内容不一致]", p[len(BASE):],
                  "local=%d remote=%d" % (len(local_txt), len(remote_txt)))
            bad += 1
    print("内容校验: %s（%d 个文件）" % ("通过" if bad == 0 else "失败 %d" % bad, len(lf)))
    return bad == 0 and not missing and (not extra or not strict)


def do_cleanup():
    open_session()
    lf, ld = local_manifest()
    rf, rd = flatten(tree())
    stale_files = sorted(p for p in set(rf) - set(lf)
                         if os.path.splitext(p)[1].lower() not in SKIP_EXT)
    for p in stale_files:
        r = manage(2, p, False)
        print("  删除文件", p[len(BASE):], r.get("code"))
    stale_dirs = sorted((set(rd) - {BASE.rstrip('/')} - set(ld)),
                        key=lambda x: x.count("/"), reverse=True)
    for p in stale_dirs:
        r = manage(2, p, True)
        print("  删除目录", p[len(BASE):], r.get("code"))
    print("清理完成: 文件 %d / 目录 %d" % (len(stale_files), len(stale_dirs)))


def _upload_entries(entries):
    """entries: [(相对路径, 本地绝对路径)]，打包成平台代码包并上传。"""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for rel, full in entries:
            z.write(full, "%d/%s" % (PROJ, rel))
    data = buf.getvalue()
    print("打包 %d 个文件 / %d 字节" % (len(entries), len(data)))

    sign = post("/api/common/upload/sign",
                {"md5": hashlib.md5(data).hexdigest() + ".zip"})
    d = sign.get("data") or {}
    if not d.get("presigned_url"):
        raise RuntimeError("上传签名失败: %s" % sign)
    req = urllib.request.Request(
        d["presigned_url"], data=data, method="PUT",
        headers={"Content-Type": "application/zip", "Content-Length": str(len(data))})
    with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(req, timeout=180) as r:
        print("OBS 上传状态", r.status)

    r = post("/api/lab/project/upload/code",
             {"project_id": PROJ, "code_url": d["key"]})
    if r.get("code") != 0:
        raise RuntimeError("代码包写入失败: %s" % r)
    print("代码包已更新:", d["key"])
    r = post("/api/project/init", {"project_id": PROJ})
    print("工作区重载:", r.get("code"))


def do_upload():
    """走平台「上传代码」链路：zip -> OBS 直传 -> ProjectUploadCode。"""
    open_session()
    entries = []
    for root, dnames, fnames in os.walk(LOCAL):
        dnames[:] = [d for d in dnames if d not in SKIP_DIRS]
        for fn in fnames:
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, LOCAL).replace("\\", "/")
            entries.append((rel, full))
    _upload_entries(entries)


def do_restore(do_build=True):
    """把备份的旧 C 工程恢复到根目录，鸿蒙文件保持不动，然后编译验证。"""
    if not os.path.isdir(RESTORE):
        raise RuntimeError("旧 C 工程备份不存在: %s" % RESTORE)
    open_session()
    rf, rd = flatten(tree())

    c_files = []
    for root, dnames, fnames in os.walk(RESTORE):
        for fn in fnames:
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, RESTORE).replace("\\", "/")
            c_files.append((rel, full))
    print("备份文件: %d" % len(c_files))

    c_dirs = set()
    for rel, _ in c_files:
        d = posixpath.dirname(rel)
        while d:
            c_dirs.add(BASE + d)
            d = posixpath.dirname(d)
    for p in sorted(c_dirs, key=lambda x: x.count("/")):
        if p in rd:
            continue
        r = None
        for _ in range(4):
            r = manage(1, p, True)
            cause = str(r.get("cause", ""))
            if r.get("code") == 0 or "已存在" in cause:
                break
            if "exit status 255" not in cause:
                break
            time.sleep(2)
        if r.get("code") != 0 and "已存在" not in str(r.get("cause", "")):
            raise RuntimeError("建目录失败 %s: %s" % (p, r))
    print("C 工程目录就绪: %d" % len(c_dirs))

    for rel, full in c_files:
        p = BASE + rel
        if p in rf:
            continue
        r = None
        for _ in range(4):
            r = manage(1, p, False)
            cause = str(r.get("cause", ""))
            if r.get("code") == 0 or "已存在" in cause:
                break
            if "exit status 255" not in cause:
                break
            time.sleep(2)
        if r.get("code") != 0 and "已存在" not in str(r.get("cause", "")):
            raise RuntimeError("建文件失败 %s: %s" % (p, r))
    print("C 工程文件已建好")

    batch, size = [], 0
    for rel, full in c_files:
        with open(full, "r", encoding="utf-8", errors="replace") as f:
            content = f.read()
        batch.append({"file_path": BASE + rel, "contents": content})
        size += len(content.encode("utf-8"))
        if size >= 400000 or len(batch) >= 30:
            _write_batch(batch); batch, size = [], 0
    if batch:
        _write_batch(batch)
    post("/api/project/save", {"project_id": PROJ})
    print("C 工程已写入并保存")

    # 合并上传：旧 C 工程 + 鸿蒙工程，保证重载后两类文件都在
    entries = list(c_files)
    have = {rel for rel, _ in entries}
    for root, dnames, fnames in os.walk(LOCAL):
        dnames[:] = [d for d in dnames if d not in SKIP_DIRS]
        for fn in fnames:
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, LOCAL).replace("\\", "/")
            if rel not in have:
                entries.append((rel, full))
    _upload_entries(entries)

    for key in ("Makefile", "app/main.c", "USER/system_stm32f4xx.c",
                "startup_stm32f407xx.s", "STM32F407ZGTx_FLASH.ld"):
        r = read_remote(BASE + key)
        assert r.get("code") == 0, "恢复后读取失败: " + key
    print("关键 C 文件校验通过")

    if do_build:
        b = post("/api/project/build", {"project_id": PROJ}, timeout=300)
        d = b.get("data") or {}
        out = "".join((x.get("out_data") or "") for x in (d.get("build_result") or []))
        print("编译: status=%s error=%s warning=%s" %
              (d.get("build_status"), d.get("error_cnt"), d.get("warning_cnt")))
        if b.get("code") != 0 or d.get("error_cnt"):
            print(out[-2500:])
            raise RuntimeError("编译失败")
        print("编译通过，固件已生成")


def main():
    import argparse
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("action", nargs="?", default="all")
    ap.add_argument("--project", type=int)
    ap.add_argument("--url")
    ap.add_argument("--base")
    ap.add_argument("--local")
    a, _ = ap.parse_known_args()
    configure(a.project, a.url, a.base, a.local)
    action = a.action
    try:
        if action == "probe":
            do_probe()
        elif action == "backup":
            do_backup()
        elif action == "push":
            do_push()
        elif action == "cleanup":
            do_cleanup()
        elif action == "verify":
            do_verify()
        elif action == "upload":
            do_upload()
        elif action == "restore":
            do_restore()
        elif action == "all":
            do_probe(); do_backup(); do_push()
            if do_verify(strict=False):
                do_cleanup()
                do_upload()
                do_verify(strict=True)
            else:
                print("校验未通过，跳过清理")
        else:
            print(__doc__)
            return 2
    finally:
        close_session()
    return 0


if __name__ == "__main__":
    sys.exit(main())
