<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

### Claude radio font

`fonts/NotoSansSC-VF.ttf` comes from Google Fonts `ofl/notosanssc`, covered by
`fonts/OFL.txt` (SIL OFL 1.1). Source SHA256:
`a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da`.
All radio fonts use a real **700 Bold** instance of this variable font. The
source's default weight is 100; converting it directly produces thin text.
`fonts/passport_font_16.c` retains the full 16 px, 2 bpp, uncompressed CJK font.
`fonts/passport_font_dashboard.c` supplies a compact 16 px, 4 bpp face with a
19 px line height, covering application Chinese text plus ASCII, temperature
and punctuation. It falls back to the full font for dynamic names.
`fonts/passport_font_14.c` contains bold ASCII and the home-screen control hints;
`fonts/passport_font_40.c` contains bold clock digits. `main/radio_font_title.c`
provides the 20 px, 4 bpp station-title face, also with full-font fallback.
These assets are compiled by `main/CMakeLists.txt`.

Reproduce with `python -m pip install fonttools==4.60.1`, then
`python tools/generate_radio_font.py` (Node/npm required; lv_font_conv pinned to
1.5.3). The generator hash-checks the licensed source and creates a temporary
static weight-700 instance with fontTools before converting all five faces.
The full font covers ASCII 0x20-0x7e, 0xb0, 0xb7, punctuation 0x3000-0x303f
and 0xff01-0xff60, and CJK 0x4e00-0x9fff. The converter omits absent source
glyphs; the UI explicitly substitutes `?` for unsupported characters, including
emoji. Enable `CONFIG_LV_FONT_FMT_TXT_LARGE` because the full bitmap exceeds
1 MiB. Coverage and compact-face metrics are checked in `tests/test_radio_font.py`.
Device rendering and runtime heap remain separate acceptance checks.
