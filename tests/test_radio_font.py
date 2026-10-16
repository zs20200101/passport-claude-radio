"""Check the generated font inventory against application Chinese text."""
from pathlib import Path
import re
import unittest
ROOT = Path(__file__).resolve().parents[1]

class FontTests(unittest.TestCase):
    def test_application_chinese_coverage(self):
        font = (ROOT/'assets/fonts/passport_font_16.c').read_text(encoding='utf-8')
        codepoints = {int(c, 16) for c in re.findall(r'/\* U\+([0-9A-Fa-f]+)', font)}
        self.assertGreater(len(codepoints), 21000)
        needed = set()
        for file in list((ROOT/'main').glob('*.cc')) + list((ROOT/'main').glob('*.h')):
            needed.update(ord(c) for c in file.read_text(encoding='utf-8') if '\u4e00' <= c <= '\u9fff')
        for name in ('bridge.py', 'companion.py'):
            needed.update(ord(c) for c in (ROOT/'tools/claude_lan_bridge'/name).read_text(encoding='utf-8') if '\u4e00' <= c <= '\u9fff')
        self.assertFalse(needed-codepoints, 'Missing codepoints: '+repr(needed-codepoints))
        self.assertIn(0xb0, codepoints)
        self.assertIn('--bpp 2', font)
        self.assertIn('--no-compress', font)

    def test_dashboard_font_metrics_and_bold_assets(self):
        # Tall rare glyphs in the full CJK inventory must not determine idle-row
        # height. Check the actual generated fonts rather than assumed font size.
        for name, limit in (("passport_font_dashboard.c", 22),
                            ("passport_font_14.c", 18), ("passport_font_40.c", 40)):
            text = (ROOT / "assets/fonts" / name).read_text(encoding="utf-8")
            height = int(re.search(r"\.line_height = (\d+)", text).group(1))
            self.assertLessEqual(height, limit)
            self.assertIn("wght=700", text)
        small = (ROOT / "assets/fonts/passport_font_14.c").read_text(encoding="utf-8")
        points = {int(c, 16) for c in re.findall(r'/\* U\+([0-9A-Fa-f]+)', small)}
        self.assertTrue(set(map(ord, "上下换台切确定播放长按进入设置单击调音量双")) <= points)
        for path in (ROOT / "assets/fonts/passport_font_16.c", ROOT / "main/radio_font_title.c"):
            self.assertIn("wght=700", path.read_text(encoding="utf-8"))

    def test_home_channel_and_maximum_volume_fit(self):
        small = (ROOT / "assets/fonts/passport_font_14.c").read_text(encoding="utf-8")
        descriptions = small.split("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {", 1)[1].split("};", 1)[0]
        advances = [int(value) for value in re.findall(r"\.adv_w = (\d+)", descriptions)]
        # This generated font begins with the contiguous ASCII range at id 1.
        for text, width in (("CH32/32", 60), ("100%", 39)):
            self.assertLessEqual(sum(advances[ord(c) - 32 + 1] for c in text) / 16, width)

if __name__ == '__main__':
    unittest.main()
