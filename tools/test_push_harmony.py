# -*- coding: utf-8 -*-
"""鸿蒙工程同步测试用例。

覆盖两类：
  本地：工程结构、JSON 配置、ArkTS 相对导入、页面注册、rawfile 单文件网页
  线上：工作区清单、旧 C 工程清理、逐文件内容、代码包可下载、代码包内容

运行：python test_push_harmony.py
退出码：0 = 全通过；1 = 有失败
"""
import io
import json
import os
import posixpath
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import push_harmony as H  # noqa: E402

RESULTS = []


class SkipTest(Exception):
    pass


def check(name, fn):
    try:
        detail = fn()
        RESULTS.append((name, "PASS", detail or ""))
    except SkipTest as e:
        RESULTS.append((name, "SKIP", str(e)))
    except AssertionError as e:
        RESULTS.append((name, "FAIL", str(e)))
    except Exception as e:
        RESULTS.append((name, "ERROR", "%s: %s" % (type(e).__name__, e)))


FW_DIR = None


def walk_dir(base):
    out = []
    for root, dnames, fnames in os.walk(base):
        dnames[:] = [d for d in dnames if d not in H.SKIP_DIRS]
        for fn in fnames:
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, base).replace("\\", "/")
            out.append((rel, full))
    return out


def walk_local():
    return walk_dir(H.LOCAL)


def firmware_files():
    return walk_dir(FW_DIR) if FW_DIR else []


def text_files():
    return [(r, f) for r, f in walk_local()
            if os.path.splitext(r)[1].lower() not in H.SKIP_EXT]


def binary_files():
    return [(r, f) for r, f in walk_local()
            if os.path.splitext(r)[1].lower() in H.SKIP_EXT]


# ───────────────────────── 本地 ─────────────────────────

def t_structure():
    required = [
        "build-profile.json5", "oh-package.json5", "hvigorfile.ts",
        "AppScope/app.json5",
        "entry/src/main/module.json5",
        "entry/src/main/ets/entryability/EntryAbility.ets",
        "entry/src/main/ets/pages/Index.ets",
        "entry/src/main/resources/base/profile/main_pages.json",
        "entry/src/main/resources/rawfile/ui/index.html",
        "ui-src/index.html", "ui-src/app.js", "ui-src/styles.css",
    ]
    missing = [p for p in required if not os.path.isfile(os.path.join(H.LOCAL, p))]
    assert not missing, "缺少关键文件: " + ", ".join(missing)
    return "%d 个本地文件" % len(walk_local())


def t_json():
    bad = []
    count = 0
    for rel, full in walk_local():
        if os.path.splitext(rel)[1].lower() not in (".json", ".json5"):
            continue
        count += 1
        try:
            with open(full, encoding="utf-8") as f:
                json.load(f)
        except Exception as e:
            bad.append("%s (%s)" % (rel, e))
    assert not bad, "JSON 解析失败: " + "; ".join(bad)
    return "%d 个配置" % count


def t_imports():
    pat = re.compile(r"""\bfrom\s+['"](\.[^'"]+)['"]""")
    bad = []
    total = 0
    for rel, full in text_files():
        if not rel.endswith(".ets"):
            continue
        with open(full, encoding="utf-8") as f:
            src = f.read()
        for target in pat.findall(src):
            total += 1
            base = posixpath.normpath(posixpath.join(posixpath.dirname(rel), target))
            candidates = [base + ".ets", base + ".ts", base + ".d.ts",
                          base + "/index.ets", base + "/index.ts"]
            if not any(os.path.isfile(os.path.join(H.LOCAL, c)) for c in candidates):
                bad.append("%s -> %s" % (rel, target))
    assert not bad, "无法解析的导入: " + "; ".join(bad)
    return "%d 条相对导入" % total


def t_pages():
    pages_json = os.path.join(
        H.LOCAL, "entry", "src", "main", "resources", "base", "profile", "main_pages.json")
    with open(pages_json, encoding="utf-8") as f:
        pages = (json.load(f).get("src") or [])
    assert pages, "main_pages.json 未注册任何页面"
    for p in pages:
        assert os.path.isfile(os.path.join(H.LOCAL, "entry", "src", "main", "ets", p + ".ets")), \
            "页面文件不存在: " + p
    index = os.path.join(H.LOCAL, "entry", "src", "main", "ets", "pages", "Index.ets")
    with open(index, encoding="utf-8") as f:
        src = f.read()
    assert "@Entry" in src and "@Component" in src, "Index.ets 缺少 @Entry/@Component"
    return "注册页面: " + ", ".join(pages)


def t_rawfile():
    p = os.path.join(H.LOCAL, "entry", "src", "main", "resources", "rawfile", "ui", "index.html")
    with open(p, encoding="utf-8") as f:
        html = f.read()
    assert len(html) > 50000, "单文件网页过小: %d" % len(html)
    assert "<style>" in html, "CSS 未内联"
    assert "HarmonyHost" in html, "未注入鸿蒙宿主桥接"
    assert 'src="./' not in html, "仍存在未内联的脚本资源"
    assert 'href="./styles.css' not in html, "仍存在未内联的样式资源"
    return "%d 字节，无外链" % len(html)


def t_build_ui():
    if not shutil.which("node"):
        raise SkipTest("未安装 Node，跳过构建产物比对")
    out = os.path.join(H.LOCAL, "entry", "src", "main", "resources", "rawfile", "ui", "index.html")
    with open(out, "rb") as f:
        before = f.read()
    r = subprocess.run(["node", "tools/build-ui.js"], cwd=H.LOCAL,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert r.returncode == 0, "build-ui.js 失败: " + r.stdout.decode("utf-8", "replace")[-500:]
    with open(out, "rb") as f:
        after = f.read()
    if before != after:
        with open(out, "wb") as f:
            f.write(before)
        raise AssertionError("rawfile/ui/index.html 与 ui-src 不同步，已还原")
    return "构建产物与 ui-src 一致"


# ───────────────────────── 线上 ─────────────────────────

def t_online_manifest():
    rf, rd = H.flatten(H.tree())
    tf = text_files()
    bf = binary_files()
    expected = tf + bf + firmware_files()
    missing = []
    for r, _ in expected:
        p = H.BASE + r
        if p in rf:
            continue
        # 目录接口不列出隐藏文件，隐藏文件用 open 接口确认
        if os.path.basename(r).startswith(".") and H.read_remote(p).get("code") == 0:
            continue
        missing.append(r)
    assert not missing, "线上缺失: " + ", ".join(missing)
    return "%d 文本 + %d 二进制 + %d C 固件" % (len(tf), len(bf), len(firmware_files()))


def t_old_cleaned():
    rf, _ = H.flatten(H.tree())
    old = [
        "app/main.c", "CORE/core_cm4.h", "USER/main.c", "Makefile", "main.c",
        "README.md", "push.py", "startup_stm32f407xx.s", "STM32F407ZGTx_FLASH.ld",
    ]
    residual = [p for p in old if H.BASE + p in rf]
    residual += [p[len(H.BASE):] for p in rf if p.startswith(
        (H.BASE + "app/", H.BASE + "CORE/", H.BASE + "USER/",
         H.BASE + "kernel/", H.BASE + "STM32F4xx_FWLib/"))]
    assert not residual, "旧 C 工程残留: " + ", ".join(sorted(set(residual))[:10])
    return "旧 C 工程文件 0 残留"


def t_firmware_restored():
    rf, _ = H.flatten(H.tree())
    fw = firmware_files()
    missing = [r for r, _ in fw if H.BASE + r not in rf]
    assert not missing, "C 固件缺失: " + ", ".join(missing[:10])
    for key in ("Makefile", "app/main.c", "USER/system_stm32f4xx.c",
                "startup_stm32f407xx.s", "STM32F407ZGTx_FLASH.ld"):
        assert H.read_remote(H.BASE + key).get("code") == 0, "关键 C 文件读取失败: " + key
    return "%d 个 C 固件文件，Makefile 存在" % len(fw)


def t_online_content():
    bad = []
    total = 0
    for rel, full in text_files() + [(r, f) for r, f in firmware_files()
                                     if os.path.splitext(r)[1].lower() not in H.SKIP_EXT]:
        total += 1
        r = H.read_remote(H.BASE + rel)
        if r.get("code") != 0:
            bad.append(rel + " 读取失败")
            continue
        with open(full, encoding="utf-8") as f:
            local = f.read().replace("\r\n", "\n")
        remote = (r["data"].get("source_code") or "").replace("\r\n", "\n")
        if local != remote:
            bad.append("%s (local=%d remote=%d)" % (rel, len(local), len(remote)))
    assert not bad, "内容不一致: " + "; ".join(bad[:5])
    return "%d/%d 文件一致" % (total, total)


_PKG_CACHE = {}


def _fetch_package():
    d = H.post("/api/lab/project/detail", {"project_id": H.PROJ})
    assert d.get("code") == 0, "项目详情失败: %s" % d
    code_url = (d.get("data") or {}).get("code_url") or ""
    assert code_url.endswith(".zip"), "code_url 未指向 zip: %r" % code_url
    if code_url in _PKG_CACHE:
        return code_url, _PKG_CACHE[code_url]
    url = "https://cdn-obs.puliedu.com/app-prod/" + code_url
    req = urllib.request.Request(url, headers={
        "User-Agent": "Mozilla/5.0",
        "Referer": "https://app.puliedu.com/",
    })
    with H.P._no_proxy.open(req, timeout=60) as r:
        data = r.read()
    _PKG_CACHE[code_url] = data
    return code_url, data


def t_code_package():
    code_url, data = _fetch_package()
    return "%s (%d 字节)" % (code_url.rsplit("/", 1)[-1], len(data))


def t_zip_entries():
    code_url, data = _fetch_package()
    z = zipfile.ZipFile(io.BytesIO(data))
    names = [n for n in z.namelist() if not n.endswith("/")]
    prefix = "%d/" % H.PROJ
    assert names and all(n.startswith(prefix) for n in names), "代码包目录结构异常"
    rels = {n[len(prefix):]: n for n in names}
    expected = text_files() + binary_files() + firmware_files()
    missing = [r for r, _ in expected if r not in rels]
    assert not missing, "代码包缺失: " + ", ".join(missing[:10])
    return "%d 个条目" % len(names)


def t_zip_content():
    code_url, data = _fetch_package()
    z = zipfile.ZipFile(io.BytesIO(data))
    bad = []
    all_files = walk_local() + firmware_files()
    for rel, full in all_files:
        entry = "%d/%s" % (H.PROJ, rel)
        with open(full, "rb") as f:
            local = f.read()
        if z.read(entry) != local:
            bad.append(rel)
    assert not bad, "代码包内容不一致: " + ", ".join(bad[:5])
    return "%d 个文件逐字节一致" % len(all_files)


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=int)
    ap.add_argument("--url")
    ap.add_argument("--firmware-dir")
    a = ap.parse_args()
    H.configure(a.project, a.url)
    global FW_DIR
    FW_DIR = a.firmware_dir or None

    local_tests = [
        ("[本地] 工程结构", t_structure),
        ("[本地] JSON/JSON5 解析", t_json),
        ("[本地] ArkTS 相对导入", t_imports),
        ("[本地] 页面注册", t_pages),
        ("[本地] rawfile 单文件网页", t_rawfile),
        ("[本地] 单文件网页与 ui-src 同步", t_build_ui),
    ]
    online_tests = [
        ("[线上] 工作区文件清单", t_online_manifest),
    ]
    if FW_DIR:
        online_tests.append(("[线上] C 固件已恢复", t_firmware_restored))
    else:
        online_tests.append(("[线上] 旧 C 工程已清理", t_old_cleaned))
    online_tests += [
        ("[线上] 逐文件内容一致", t_online_content),
        ("[线上] 代码包可下载", t_code_package),
        ("[线上] 代码包文件清单", t_zip_entries),
        ("[线上] 代码包内容一致", t_zip_content),
    ]

    for name, fn in local_tests:
        check(name, fn)

    session_started = False
    try:
        H.open_session()
        session_started = True
        for name, fn in online_tests:
            check(name, fn)
    except Exception as e:
        RESULTS.append(("[线上] 会话", "ERROR", "%s: %s" % (type(e).__name__, e)))
    finally:
        if session_started:
            H.close_session()

    width = max(len(n) for n, _, _ in RESULTS) + 2
    print("=" * (width + 40))
    for name, status, detail in RESULTS:
        print("%s %s  %s" % (name.ljust(width), status.ljust(5), detail))
    print("=" * (width + 40))
    if FW_DIR:
        print("说明: C 固件已恢复，平台编译需单独执行 push_harmony.py restore 验证")
    else:
        print("说明: [线上] STM32 编译不执行 —— 鸿蒙 ArkTS 工程不在该实验的 C 工具链内")

    failed = [r for r in RESULTS if r[1] in ("FAIL", "ERROR")]
    skipped = [r for r in RESULTS if r[1] == "SKIP"]
    passed = [r for r in RESULTS if r[1] == "PASS"]
    print("结果: %d 通过 / %d 失败 / %d 跳过" % (len(passed), len(failed), len(skipped)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
