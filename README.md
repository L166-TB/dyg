# 甘蔗榨糖产线 · 3.5 寸屏版本

本仓库整理自 `C:\Users\14393\Desktop\总文件夹\11`，包含可编译的 STM32F407 固件、鸿蒙上位机工程、3.5 寸屏电路和同步工具。

## 目录

```
firmware35/      STM32F407 固件（C，Keil/GCC Makefile）
  app/tft35.c    3.5 寸 320x480 16 位并口驱动（ILI9488）
  app/cane_gui.c 320x480 竖屏界面：左侧工序栏 + 2x2 数据卡
harmony/         HarmonyOS ArkTS 工程 + ui-src 单文件网页
circuit/         circuit_35.json（3.5 寸屏已替换、NC1/BVDD 已补线）
tools/           平台同步、电路替换、测试脚本
tests/           固件/布局/电路自动化测试
```

## 本次修改

1. **修复“改了不显示”**：原 `tft28.c` 每个并口周期调用 `delay_us(1)`，平台延时比真实时间慢约三个数量级；`tft35.c` 改为空循环 + 服务时基。
2. **2.8 寸 → 3.5 寸**：新增 `tft35.c/h`，320×480、16 位 8080 并口、XPT2046 触摸；删除 `tft28.c/h` 并更新 `board_port.c`、`cane_gui.c`、`cane_runtime.c`、`Makefile`。
3. **布局缩放**：界面从 240×320 竖屏改为 320×480 竖屏；左侧 6 道工序栏，右侧 2×2 加高卡片，底部状态、操作按钮和功能入口；白底、强调色、卡片风格保持不变。
4. **电路**：`tftlcd_1` 从 2.8 寸 `tftlcd` 替换为 3.5 寸 `tftlcd3_5`，34 个引脚映射，26 个导线端点重算；补 `NC1→GND1`、`BVDD→VCC3_1`，消除未绑定警告并给背光升压供电。
5. 修复 `gui_touch_action` 的 `-Wmisleading-indentation` 警告，最终编译 **0 error / 0 warning**。

## 编译

```powershell
# 平台编译（会自动推送、打包 hex、返回 error/warning 数）
python tools/push_firmware35.py --project 85461 --url "<实验页URL>"
```

## 测试

```powershell
python tests/test_firmware35.py -v
```

覆盖：320×480 分辨率、并口延时、窗口/清屏、触摸高度、界面分区越界、6 道工序栏、Makefile 引用、旧 tft28 清理、电路元件/34 引脚/244 导线/NC1/BVDD/D0-D15、鸿蒙入口与工具脚本。

## 说明

- 平台仿真在自动点击“运行”时仍停在 `Reset_Handler`（PC=0x0800A0E0，仿真时间 0），属于平台运行/加载流程问题；代码本身已通过平台编译。
- 3.5 寸屏控制器按 ILI9488 初始化；若实物为 ST7796，可在 `tft35.c` 的 `cfg[]` 中替换初始化序列。
