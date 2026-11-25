# -*- coding: utf-8 -*-
"""3.5 寸屏固件与电路归档测试。

运行：python tests/test_firmware35.py -v
"""
import json
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware35")
APP = os.path.join(FW, "app")


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


class TftDriverTest(unittest.TestCase):
    def test_resolution_is_320x480(self):
        h = read(os.path.join(APP, "tft35.h"))
        self.assertIn("#define TFT35_WIDTH 320u", h)
        self.assertIn("#define TFT35_HEIGHT 480u", h)

    def test_bus_has_no_slow_delay(self):
        c = read(os.path.join(APP, "tft35.c"))
        self.assertNotIn("delay_us(1)", c)
        self.assertIn("static void dly(void)", c)
        self.assertIn("board_millis()", c)

    def test_full_screen_window_and_clear(self):
        c = read(os.path.join(APP, "tft35.c"))
        self.assertIn("0x01);data(0x3f)", c)
        self.assertIn("0x01);data(0xdf)", c)
        self.assertIn("153600u", c)

    def test_touch_maps_full_height(self):
        c = read(os.path.join(APP, "tft35.c"))
        self.assertIn("*y=(uint32_t)ay*479u/4095u", c)

    def test_old_28_driver_removed(self):
        for name in ("tft28.c", "tft28.h"):
            self.assertFalse(os.path.exists(os.path.join(APP, name)))
        for name in ("tft35.c", "tft35.h"):
            self.assertTrue(os.path.exists(os.path.join(APP, name)))


class GuiLayoutTest(unittest.TestCase):
    def setUp(self):
        self.src = read(os.path.join(APP, "cane_gui.c"))

    def test_zone_fits_screen(self):
        m = re.search(r"static const GuiRect zone\[\]\s*=\s*\{(.*?)\};", self.src, re.S)
        self.assertIsNotNone(m)
        rects = [tuple(map(int, x)) for x in re.findall(r"\{(\d+),(\d+),(\d+),(\d+)\}", m.group(1))]
        self.assertEqual(len(rects), 11)
        for x, y, w, h in rects:
            self.assertLessEqual(x + w, 320, (x, y, w, h))
            self.assertLessEqual(y + h, 480, (x, y, w, h))

    def test_sidebar_has_six_process_tabs(self):
        self.assertIn('"称重","榨提","煮糖","分装","冷却","入库"', self.src)
        self.assertIn("i*60u", self.src)

    def test_touch_uses_480_height(self):
        self.assertIn("y<396", self.src)
        self.assertIn("y>=438", self.src)
        self.assertIn("*y", read(os.path.join(APP, "tft35.c")))

    def test_render_clears_480_lines(self):
        self.assertIn("r->y+r->h:480", self.src)


class BuildConfigTest(unittest.TestCase):
    def test_makefile_uses_tft35(self):
        mk = read(os.path.join(FW, "Makefile"))
        self.assertIn("app/tft35.c", mk)
        self.assertNotIn("app/tft28.c", mk)

    def test_no_tft28_references(self):
        for name in os.listdir(APP):
            if not name.endswith((".c", ".h")):
                continue
            text = read(os.path.join(APP, name))
            self.assertNotIn("tft28", text, name)
            self.assertNotIn("TFT28", text, name)

    def test_compile_record(self):
        path = os.path.join(ROOT, "BUILD_STATUS.md")
        self.assertTrue(os.path.exists(path))
        text = read(path)
        self.assertIn("0 error", text)
        self.assertIn("0 warning", text)


class CircuitTest(unittest.TestCase):
    def setUp(self):
        with open(os.path.join(ROOT, "circuit", "circuit_35.json"), encoding="utf-8") as f:
            self.c = json.load(f)

    def test_tft_is_35_inch(self):
        nodes = [n for n in self.c["nodes"] if n.get("name") == "tftlcd_1"]
        self.assertEqual(len(nodes), 1)
        n = nodes[0]
        self.assertEqual(n["component_id"], "tftlcd3_5")
        self.assertEqual(n["component_name"], "3.5寸显示屏")
        self.assertEqual(len(n["index"]), 34)

    def test_wire_count(self):
        self.assertEqual(len(self.c["allPointes"]), 244)

    def test_nc1_and_bvdd_bound(self):
        texts = set()
        for w in self.c["allPointes"]:
            for side in ("startNode", "endNode"):
                t = (w.get(side) or {}).get("text")
                if t:
                    texts.add(t)
        for pin in ("NC1", "BVDD", "GND1", "VCC3_1"):
            self.assertIn("tftlcd_1." + pin, texts)

    def test_data_bus_d0_d15(self):
        texts = {t for t in (
            (w.get("startNode") or {}).get("text") for w in self.c["allPointes"]
        ) if t}
        texts |= {t for t in (
            (w.get("endNode") or {}).get("text") for w in self.c["allPointes"]
        ) if t}
        for i in range(16):
            self.assertIn("tftlcd_1.D%d" % i, texts)


class PackageTest(unittest.TestCase):
    def test_harmony_entry_exists(self):
        for p in ("AppScope/app.json5", "entry/src/main/ets/pages/Index.ets",
                  "entry/src/main/resources/rawfile/ui/index.html"):
            self.assertTrue(os.path.exists(os.path.join(ROOT, "harmony", p)), p)

    def test_tool_scripts_exist(self):
        for p in ("puli_session.py", "push_firmware35.py", "push_circuit35.py",
                  "fix_circuit_wires.py", "test_push_harmony.py"):
            self.assertTrue(os.path.exists(os.path.join(ROOT, "tools", p)), p)


if __name__ == "__main__":
    unittest.main(verbosity=2)
