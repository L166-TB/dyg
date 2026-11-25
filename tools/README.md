# puliedu 电路仿真工具包

给需要在 `app.puliedu.com` 电路仿真实验里**用脚本操作**（而不是鼠标拖线）的人 / AI Agent。
零依赖（Node ≥ 22），不需要 Playwright/Puppeteer。

包含两套东西：
1. **接线自动化 CLI** —— 读/改/保存画布接线
2. **可视化控制台**（`console/`）—— 把实验数据渲染成中文图形界面，带局部脏刷新与动画

---

## 一、可视化控制台（`console/`）

把实验的 128 元件 / 183 导线渲染成中文仪表盘。**双击 `console/index.html` 即可打开**。

```powershell
# 1) 从实验页导出数据（需要已登录的 Edge）
python puli_export.py --project 54639

# 2) 打开控制台（直开即可，无构建步骤）
start console\index.html

# 3) 看测试用例（27 条，全绿）
start console\test.html
#    或者在命令行跑：
node _run_tests.js
```

### 界面结构

| 区域 | 内容 |
|---|---|
| 顶部概览条 | 元件数 / 导线数 / 已接引脚 / 类别数 / 空闲引脚 |
| 左侧元件面板 | 按类别分组（主控·传感器·执行器·显示·通信·输入·电源·基础元件），带色标 |
| 中间网络拓扑图 | SVG 力导向图：主控居中，其它元件按类别成环排布，连线=电气连接 |
| 右侧数据总览 | 分类构成条形图 + 引脚连通率 + 中文覆盖率 |
| 底部连接清单 | 引脚级明细表（中文元件名 + 原始引脚名） |

### 三条需求是怎么落地的

| 需求 | 实现 | 验证方式 |
|---|---|---|
| **1. 可视化图形界面** | 5 区域仪表盘（`index.html` + `views.js`） | 真实浏览器断言：128 元件项 / 183 拓扑线 / 128 拓扑节点全部渲染 |
| **2. 局部脏刷新** | 每区域一个**数据签名**（`store.js`），签名不变则不重画（`dirty.js`） | 实测：只改日志 → 只重画 1 区；空跑一轮 → 0 次重画 |
| **3. 多动画演示** | 4 类：电流脉冲 / 元件呼吸 / 数字滚动 / 拓扑生长（`anim.js`） | 用例 D4–D6 断言状态机与开关策略 |
| **4. 中文演示** | 29 种元件 + 8 类别的中文映射（`zh.js`） | 用例 B1–B6 断言无遗漏、无 undefined 泄漏 |
| **8. 测试用例** | 27 条断言，4 组（`test.js` + `test.html`） | `node _run_tests.js` → 退出码 0 |

### 局部脏刷新的原理（需求 2 核心）

```
每个区域声明「我依赖哪些数据」→ 算出指纹字符串
指纹没变 = 这块不用重画
指纹变了 = 只重画这块
```

实测数据（真实浏览器里点 3 次「模拟数据变化」）：

| 操作 | 重画区域 |
|---|---|
| 改日志 + 加导线 | `header, center, right, bottom`（**左侧元件面板没动**） |
| 撤销导线 | `header, center, right, bottom` |
| 只改日志 | `bottom`（**只 1 个区**） |
| 什么都不做 | 无 |

对比：若不做脏刷新，每个区每轮都重画 → 5×N 次 DOM 操作。

---

## 二、接线自动化 CLI

```powershell
# 起一个带调试端口的副本浏览器，打开实验页（不动你正在用的浏览器）
python puli_session.py list --component rgb_led_1

# 接线并保存（LED 的 B 脚 → STM32 的 PA4；--replace = 改接不并接）
python puli_session.py connect --from rgb_led_1.B --to STM32F103C8_1.PA4 --replace --save

# 纯接口复验（不需要浏览器界面，退出码 0 = 通过）
python puli_session.py verify --expect rgb_led_1.B=STM32F103C8_1.PA4
```

Node 版等价命令（`puliedu_circuit_cli.js`）：

```powershell
node puliedu_circuit_cli.js launch --url "<实验页URL>"
node puliedu_circuit_cli.js list --component rgb_led_1
node puliedu_circuit_cli.js connect --from rgb_led_1.B --to STM32F103C8_1.PA4 --replace --save
node puliedu_circuit_cli.js verify --expect rgb_led_1.B=STM32F103C8_1.PA4
```

---

## 三、文件清单

### 数据通道
| 文件 | 说明 |
|---|---|
| `puli_session.py` | ★ Python 一体化会话：启浏览器 + list / eval / verify / connect（同进程完成） |
| `puli_export.py` | 导出实验快照 → `snapshot_*.json`（控制台的数据源） |
| `puliedu_circuit_cli.js` | ★ Node 主工具：`launch` / `list` / `connect` / `verify` |
| `launch_cdp_fixed.py` | 独立 CDP 启动器（修正版，剔除 LOCK 文件） |
| `close_cdp_browser.js` | 只关工具起的那个副本浏览器 |
| `shot_clip.js` | CDP 区域截图（省流量：`node shot_clip.js out.png x y w h 缩放 [端口]`） |

### 可视化控制台（`console/`）
| 文件 | 说明 |
|---|---|
| `index.html` | 主界面（三栏仪表盘） |
| `test.html` | 测试用例可视化面板 |
| `css/app.css` | 浅色主题样式 |
| `js/zh.js` | 中文映射（元件/类别/引脚） |
| `js/store.js` | 数据仓库 + 区域签名 |
| `js/dirty.js` | 局部脏刷新调度器 |
| `js/anim.js` | 动画引擎（4 类） |
| `js/views.js` | 5 个区域的渲染函数 |
| `js/app.js` | 主入口（装配 + 交互） |
| `js/test.js` | 27 条测试用例 |
| `js/fallback.js` | 内嵌兜底数据（file:// 直开时用，由 `make_fallback.py` 生成） |
| `DESIGN.md` | 设计说明（需求拆解、架构、脏刷新设计） |

### 验证工具
| 文件 | 说明 |
|---|---|
| `_run_tests.js` | 命令行跑 27 条用例（headless，退出码 0 = 全通过） |
| `_verify_page.py` | 真实浏览器 DOM 断言（渲染结果 + 脏刷新行为） |
| `_shot_console.py` | 截图控制台/测试页 |
| `make_fallback.py` | 从快照生成内嵌兜底数据 |

### 文档
| 文件 | 说明 |
|---|---|
| `PLAYBOOK_接线自动化.md` | ★ 实战手册：命令详解、原理、**踩坑清单**、**省 token 规范** |
| `PULIEDU_CIRCUIT_API.md` | 接口逆向记录（HTTP/WS 清单、电路数据模型、保存链路） |
| `console/DESIGN.md` | 控制台设计说明 |

---

## 四、注意

- **只适用于电路仿真类实验**（`lab_type=2`）。AGI/机械臂那种 Unity 3D 场景不适用。
- 别把 token 提交进仓库：工具默认从已登录页面的 `localStorage` 现取。
- 建议先用 `--dry-run` 看要写入的导线，再决定是否 `--save`。
- 控制台用 `file://` 直开即可；若浏览器拦截本地 fetch，会自动降级到内嵌数据（不会白屏）。

---

## 五、实测记录

**接线（2026-10-13）**：MODBUS 实验 `project_id=49037`，`rgb_led_1.B` 从 PA2 改接到 PA4，
往返测试 `B→PA5 → B→PA4` 验证可写可回滚，`verify` → PASS。

**控制台（2026-10-14）**：导出「鸿蒙」实验 `project_id=54639`（GD32VF103V 智慧农业环境监测系统）
的完整快照 —— **128 元件 / 183 导线 / 353 引脚 / 325 已接**。
控制台真实浏览器实测：128 元件项、128 拓扑节点、183 拓扑线、8 个类别分组全部正确渲染；
27 条测试用例全绿；局部脏刷新实测只重画该变的区域。
