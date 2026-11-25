# puliedu 电路仿真（MODBUS 通信协议实验）接口逆向 + LED B → STM32 PA4 接线

- 目标页面：`https://app.puliedu.com/#/simulation?__params=...`
  - 解出来是 `{"project_id":"49037","lab_id":"31773","lab_name":"MODBUS通信协议实验","lab_type":"2"}`
- 工具：**OpenCLI v1.8.8**（`npm i -g @jackwener/opencli`）+ 其 Browser Bridge 扩展 v1.0.24
- 结果：**LED 的 B 脚已接到 STM32 的 PA4**，画布已刷新、已保存到服务器，硬刷新后依然生效。

> 📌 **想直接复用这套方法（含一键命令、省 token 规范、踩坑清单）→ 看
> [`PLAYBOOK_接线自动化.md`](PLAYBOOK_接线自动化.md) 和零依赖工具 [`puliedu_circuit_cli.js`](puliedu_circuit_cli.js)。**
> 本文是逆向过程与接口细节的记录。

---

## 一、OpenCLI 怎么接上的（踩坑记录）

OpenCLI 的 `opencli browser *` 命令族**只走 Browser Bridge 扩展**，不认 `OPENCLI_CDP_ENDPOINT`
（CDP 模式只对 site adapter 类命令生效）。而本机 Chrome/Edge 都没装该扩展，`opencli doctor` 一开始报
`Extension: not connected`。

做法（脚本已留在本目录）：

1. `npm install -g @jackwener/opencli`（v1.8.8，daemon 起在 19825）
2. 从 GitHub Release 下载扩展：`opencli-extension-v1.0.24.zip`
   → 解压到 `puliedu_probe\opencli-ext\`
3. `node launch_edge_cdp.js <url> 9222`
   - 复制当前 Edge 的登录态（Local Storage / Session Storage / IndexedDB / Cookies …）到临时 profile
   - 用 `--load-extension=<opencli-ext>` + `--remote-debugging-port=9222` 启动**副本** Edge
   - 原 Edge 不受影响（同一 user-data-dir 会被锁，所以必须复制）
4. `opencli doctor` → `[OK] Extension: connected (v1.0.24)`，之后 `opencli browser puli <cmd>` 全部可用

常用命令：

```powershell
opencli browser puli open <url>          # 打开/导航
opencli browser puli state               # 页面结构 + 可点元素编号
opencli browser puli click <ref|--text>  # 点击
opencli browser puli eval '<js>'         # 页面上下文执行 JS
opencli browser puli network [--detail "<METHOD host/path>"] [--since 10m]
opencli browser puli screenshot out.png
```

> 注意：`network` 是**排空式**缓冲，抓完一次就没了；`--detail <key>` 可在 TTL 内回捞 body。

---

## 二、该页面用到的接口（抓包实证）

| 接口 | 方法 | 作用 |
|---|---|---|
| `/api/user/current`、`/api/user/detail` | POST | 用户信息 |
| `/api/lab/project/detail` | POST | 实验项目详情（决定加载哪个电路 view 文件） |
| `/api/project/init` | POST `{project_id}` | 初始化工程（回 `user_id/role_max/org_id`） |
| `/api/project/directory` | POST | 工作区文件树（`/opt/lab/workspace/60253/49037/`） |
| `/api/project/file/open`、`/api/project/file/modify`、`/api/project/file/manage` | POST | 代码文件读写/管理 |
| `/api/project/build` | POST | 编译固件 |
| `/api/resource/component/list`、`/api/resource/component/group/list` | POST | 器件库（左侧元件面板） |
| `/api/common/system/config`、`/api/token/balance` | POST | 系统配置 / 令牌余额 |
| `/api/common/upload/sign` | POST | 取 OBS 直传签名 |
| `PUT https://obs.puliedu.com/app-prod/<view_url>?X-Amz-...` | PUT | 上传电路 JSON（画布快照） |
| **`/api/lab/project/upload/view`** | POST `{chip_id, project_id, view_url, is_default_code?}` | **保存电路（接线）** |
| `wss://app.puliedu.com/ws/emulator?user_id=&project_id=&token=` | WS | 仿真协议（`cmd 1001–1010`，见 `websock-*.js`） |

保存电路的真实链路（点「保存」时抓到）：

```
POST /api/common/upload/sign            → 拿签名
PUT  obs.puliedu.com/app-prod/org/10036/60253/json/<hash>.json   → 上传 {nodes,allPointes,breakPoints,shapeObj,svgSet}
POST /api/lab/project/upload/view       → {chip_id:"STM32F103C8", project_id:49037,
                                           view_url:"org/10036/60253/json/<hash>.json"} → {"code":0,"msg":"成功"}
```

---

## 三、电路数据模型（接线就是改这个 JSON）

电路源文件 = CDN 上的一个 JSON（本次实验默认模板：
`https://cdn-obs.puliedu.com/app-prod/resource/json/72df3a4cc61f60d83017b0af00082c14.json`），结构：

```jsonc
{
  "nodes": [ { "component_id": "rgb_led", "name": "rgb_led_1", "x": -1362, "y": 400,
               "width": 131, "height": 152.75, "kindof": "peripheralsConfig",
               "index": [ {"x":29,"y":141,"name":"R","direction":"bottom"},
                          {"x":53,"y":141,"name":"GND","direction":"bottom"},
                          {"x":73,"y":141,"name":"G","direction":"bottom"},
                          {"x":97,"y":141,"name":"B","direction":"bottom"} ] } ],
  "allPointes": [ { "points": ["-1334,358","-1334,600","-1262,600","-1262,544"],
                    "startNode": {"nodeIndex":0,"portIndex":13,"x":-1334,"y":358,"name":"pin","text":"STM32F103C8_1.PA4"},
                    "endNode":   {"nodeIndex":16,"portIndex":3,"x":-1262,"y":544,"name":"pin","text":"rgb_led_1.B"},
                    "stroke": "#519596", "strokeWidth": 2 } ],
  "breakPoints": [ {"x":-518,"y":376,"id":"Node_1"}, {"x":-502,"y":416,"id":"Node_2"} ],
  "shapeObj": {...}, "svgSet": {...}
}
```

要点：

- **一根导线 = `allPointes` 里的一个元素**；`startNode/endNode` 用 `nodeIndex`（`nodes` 下标）+
  `portIndex`（`node.index` 下标）定位引脚，`text` 是 `"实例名.引脚名"`。`points` 只是画线用的折线。
- **引脚绝对坐标** = `node.x + node.index[p].x`（+ 一个小的绘制偏移：STM32 符号是 +2，多数元件 +3；
  以同元件既有导线的坐标为准最稳）。
- **网络语义**：导线把两个引脚连成一个 net；多根导线接到同一个 `breakPoints` 里的命名节点
  （如 `Node_1`）就是同一网络（485 的 A/B 总线就是这么并的）。
- `nodes[].index` 里能查到引脚名：STM32F103C8_1 的 `PA4` = `portIndex 13`（本地坐标 50,198，方向 bottom）。

---

## 四、这次做的操作（LED.B → PA4）

原接线：`PA0→R`、`PA1→G`、**`PA2→B`**、`GND→电阻→GND`。

执行（页面上下文，等价于在画布上拖一根线）：

```js
const c = window.__CIRC;                       // 画布组件实例（从 Vue 树里按 _data.allPointes 找到）
const i = 20;                                  // 下标 20 就是那根 PA2→B 的线
const w = JSON.parse(JSON.stringify(c.allPointes[i]));
w.startNode = { nodeIndex: 0, portIndex: 13, x: -1334, y: 358, name: 'pin', text: 'STM32F103C8_1.PA4' };
w.points    = ['-1334,358', '-1334,600', '-1262,600', '-1262,544'];
c.$set(c.allPointes, i, w);
c.checkCanvasChanged();                        // 标记画布已改 → 「保存」才会真的存
c.$forceUpdate();                              // 画布立即重绘
```

然后点顶部「保存」，走平台自己的保存接口（见第二节）。

**结果**：`PA2→B` 被改成 `PA4→B`（是"改接"而不是"并接"，避免 PA2/PA4 被短接）。
如果本来就想保留 PA2，另加一根线即可（`c.allPointes.push({...})` 后 `checkCanvasChanged()` + 保存）。

---

## 五、验证证据

1. 画布重绘：DOM 里出现新路径
   `M-1334,358 L-1334,590 Q-1334,600 -1324,600 L-1272,600 Q-1262,600 -1262,590 L-1262,544`
2. 保存接口返回 `{"code":0,"msg":"成功"}`，上传文件
   `org/10036/60253/json/27a2434189c28295c766f68761e0d624.json`
3. **硬刷新**（先回 `https://app.puliedu.com/` 再进仿真页）后重新读取：
   - 页面实际加载的电路文件变成 `.../org/10036/60253/json/27a2434189c28295c766f68761e0d624.json`
   - 该文件内容里 `allPointes` 有 `STM32F103C8_1.PA4 -> rgb_led_1.B`
4. 本地留档对比（`node diff_circuit.js`）：

```
circuit_default_resource.json : PA2 -> rgb_led_1.B
circuit_project_view.json     : PA4 -> rgb_led_1.B     ← 服务器上当前生效的
```

5. 截图：`zoom_led_pa4.png`（放大后可见 PA4 引脚 → 向下 → 右拐 → 接 LED 的 B 脚）

6. **脱离浏览器、纯接口复验**（`verify_wiring.js`，退出码 0 = PASS）：

```powershell
$env:PULI_TOKEN = (opencli browser puli eval "localStorage.getItem('token')").Trim('"')
node verify_wiring.js      # → exit code 0
```

```
[1] POST /api/lab/project/detail  ->  code=0  chip_id=STM32F103C8
    view_url = org/10036/60253/json/27a2434189c28295c766f68761e0d624.json
[2] GET  https://cdn-obs.puliedu.com/app-prod/org/10036/60253/json/27a2434189c28295c766f68761e0d624.json
    nodes=23  wires=33  breakPoints=2
[3] LED 相关接线：
    STM32F103C8_1.PA0 -> rgb_led_1.R   [-1378,302 -1398,302 -1398,570 -1330,570 -1330,544]
    STM32F103C8_1.PA1 -> rgb_led_1.G   [-1378,314 -1388,314 -1388,584 -1286,584 -1286,544]
    STM32F103C8_1.PA4 -> rgb_led_1.B   [-1334,358 -1334,600 -1262,600 -1262,544]   <== B
    rgb_led_1.GND -> Resistor_SMD_3.lPin   [-1306,544 -1306,618]

RESULT: PASS — rgb_led_1.B <-> STM32F103C8_1.PA4
```

（原始输出留档：`verify_output.txt`）

| 文件 | 说明 |
|---|---|
| `page_01.png` | 打开实验页（改动前） |
| `page_03_after_reload.png` | 硬刷新后的整页 |
| `zoom_led_pa4.png` | 放大：PA4 ↔ LED.B 的新导线 |
| `net_01.json` / `net_02_save.json` | OpenCLI 抓的网络（页面加载 / 保存） |
| `circuit_default_resource.json` | 实验默认电路（原始，PA2→B） |
| `circuit_project_view.json` | 保存后项目当前电路（PA4→B） |

---

## 六、脚本清单（`C:\Users\Administrator\project_1\puliedu_probe\`）

| 脚本 | 用途 |
|---|---|
| `launch_edge_cdp.js` | 复制 Edge 登录态 + 加载 opencli 扩展 + 开 CDP 端口启动副本浏览器 |
| `close_cdp_browser.js` | 只关这个副本浏览器（CDP `Browser.close`） |
| `shot_clip.js` | CDP 区域截图（可指定 tab、缩放倍数） |
| `diff_circuit.js` | 对比两份电路 JSON 的接线差异 |
| `verify_wiring.js` | **纯接口复验**：`/api/lab/project/detail` → CDN view JSON → 断言 `rgb_led_1.B ↔ STM32F103C8_1.PA4`（PASS 退出码 0） |
| `verify_output.txt` | 上面这次复验的原始输出 |
| `websock.js` / `index-*.js` / `connection-*.js` | 从站点拉下来的前端 chunk（逆向用） |
| `opencli-ext/` | OpenCLI Browser Bridge 扩展 v1.0.24 |

复现一条链路：

```powershell
node launch_edge_cdp.js "https://app.puliedu.com/#/simulation?__params=<base64>" 9222
opencli doctor
opencli browser puli state
opencli browser puli network --since 10m
```

---

## 七、追加：工具化 + 往返实测（2026-10-13 晚）

把上面这套流程封成了零依赖工具 **`puliedu_circuit_cli.js`**（`launch` / `list` / `connect` / `verify`），
使用说明见 **[`PLAYBOOK_接线自动化.md`](PLAYBOOK_接线自动化.md)**。

工具自测（一轮真实往返，验证"能写、能改、能回滚"）：

```
connect --from rgb_led_1.B --to STM32F103C8_1.PA5 --replace --save
  → action=replaced saved=true view_url=org/10036/60253/json/332ecdba1930ad218df2315763870597.json
verify --expect rgb_led_1.B=STM32F103C8_1.PA5        → PASS (exit 0)
connect --from rgb_led_1.B --to STM32F103C8_1.PA4 --replace --save
  → action=replaced saved=true view_url=org/10036/60253/json/6be42815d651411ef9d8f00047d892e7.json
verify --expect rgb_led_1.B=STM32F103C8_1.PA4        → PASS (exit 0)
```

即：**项目当前生效的电路文件 = `org/10036/60253/json/6be42815d651411ef9d8f00047d892e7.json`，
其中 `rgb_led_1.B ↔ STM32F103C8_1.PA4`**。本文第五节提到的 `27a2434…json` 是当时那一步的版本，
之后又被往返测试覆盖了两次——这是正常现象：**平台每次保存都会生成一个新的 view 文件并更新项目指针**。
