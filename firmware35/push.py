"""蔗糖制作系统（project 60275）一键推送 + 编译。

用法：
    python push.py                 # 用同目录/上一级的 _tok.txt
    python push.py <TOKEN>         # 直接给 token

流程（2026-10-30 重建后）：
    1. 把本目录的工程打成 zip
    2. POST /api/common/upload/sign 拿直传签名 -> PUT obs.puliedu.com
    3. POST /api/lab/project/upload/code 设为该工程的代码包
    4. POST /api/project/init 让平台把工作区铺成这个包
    5. POST /api/project/build 编译，打印错误/告警数与产物体积

为什么不逐个文件推：/api/project/file/modify 只能改“已存在”的文件，
新建文件会返回 is_success=1 但实际不落盘（实测）。所以整包上传才是可靠路径。
"""
import hashlib, io, json, os, sys, urllib.request, urllib.error, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = 60275
TOK = (sys.argv[1] if len(sys.argv) > 1 else "").strip()
if not TOK:
    for p in (os.path.join(HERE, "_tok.txt"), os.path.join(os.path.dirname(HERE), "_tok.txt")):
        if os.path.exists(p):
            TOK = open(p, encoding="utf-8").read().strip()
            break
if not TOK:
    sys.exit("找不到 token：放到 _tok.txt 里，或 python push.py <TOKEN>")

FW = ["misc", "stm32f4xx_rcc", "stm32f4xx_gpio", "stm32f4xx_usart",
      "stm32f4xx_tim", "stm32f4xx_syscfg", "stm32f4xx_exti", "stm32f4xx_spi"]


def collect():
    rel = ["Makefile", "startup_stm32f407xx.s", "STM32F407ZGTx_FLASH.ld", "README.md", "push.py"]
    rel += ["CORE/" + f for f in sorted(os.listdir(os.path.join(HERE, "CORE")))]
    rel += ["STM32F4xx_FWLib/inc/" + f for f in sorted(os.listdir(os.path.join(HERE, "STM32F4xx_FWLib", "inc")))]
    rel += ["STM32F4xx_FWLib/src/%s.c" % n for n in FW]
    rel += ["USER/" + f for f in ("stm32f4xx.h", "stm32f4xx_conf.h", "stm32f4xx_it.h",
                                 "system_stm32f4xx.c", "system_stm32f4xx.h")]
    rel += ["app/" + f for f in sorted(os.listdir(os.path.join(HERE, "app"))) if f.endswith((".c", ".h"))]
    # 内核层：kernel/include/*.h + kernel/*.c（接口对标 OpenHarmony LiteOS-M）
    rel += ["kernel/include/" + f for f in sorted(os.listdir(os.path.join(HERE, "kernel", "include"))) if f.endswith(".h")]
    rel += ["kernel/" + f for f in sorted(os.listdir(os.path.join(HERE, "kernel"))) if f.endswith(".c")]
    return [r for r in rel if os.path.exists(os.path.join(HERE, r.replace("/", os.sep)))]


def post(path, body, t=600):
    req = urllib.request.Request(
        "https://app.puliedu.com" + path, data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json", "Authorization": TOK,
                 "User-Agent": "Mozilla/5.0", "Origin": "https://app.puliedu.com"},
        method="POST")
    try:
        with urllib.request.urlopen(req, timeout=t) as r:
            return json.loads(r.read().decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        return {"http": e.code, "body": e.read()[:400].decode("utf-8", "replace")}
    except Exception as e:
        return {"err": str(e)[:250]}


rel = collect()
buf = io.BytesIO()
with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
    for r in rel:
        z.write(os.path.join(HERE, r.replace("/", os.sep)), "60275/" + r)
data = buf.getvalue()
print("pack  %d 个文件  %d 字节" % (len(rel), len(data)))

sign = post("/api/common/upload/sign", {"md5": hashlib.md5(data).hexdigest() + ".zip"})
d = sign.get("data") or {}
if not d.get("presigned_url"):
    sys.exit("签名失败: " + json.dumps(sign, ensure_ascii=False)[:200])
req = urllib.request.Request(d["presigned_url"], data=data, method="PUT",
                             headers={"Content-Type": "application/zip",
                                      "Content-Length": str(len(data))})
with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(req, timeout=180) as r:
    print("upload", r.status)

print("setcode", json.dumps(post("/api/lab/project/upload/code",
                                 {"project_id": PROJ, "code_url": d["key"]}), ensure_ascii=False)[:120])
print("init   ", json.dumps(post("/api/project/init", {"project_id": PROJ}), ensure_ascii=False)[:120])

b = post("/api/project/build", {"project_id": PROJ})
bd = b.get("data") or {}
out = "".join([(x.get("out_data") or "") for x in (bd.get("build_result") or [])])
print("build  status=%s error=%s warning=%s" % (bd.get("build_status"), bd.get("error_cnt"), bd.get("warning_cnt")))
if bd.get("error_cnt"):
    print(out[-1500:])
    sys.exit(1)
print("OK    main.elf 已生成")
