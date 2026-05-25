# UI 字体生成说明

Launcher 默认 UI 字体为 **凤凰点阵 Vonwaon Bitmap 12px**（`ui_font_vonwaon`），由本目录脚本从源 TTF 转为 LVGL C 数组。

## 目录结构

```
launcher/scripts/
  FONT.md                 # 本说明
  regen_font.mjs          # 生成脚本
  fonts/
    VonwaonBitmap-12px.ttf   # 源字体（需保留在仓库中）
```

生成结果写入：`launcher/main/fonts/ui_font_vonwaon.c`

## 依赖

- **Node.js**（`npx lv_font_conv`）
- **Python 3** + **fontTools**（`pip install fonttools`）

Montserrat 14 作为 fallback，由 ESP-IDF LVGL 组件提供，无需本地 TTF。

## 重新生成

在 `launcher/scripts` 目录下：

```bash
pnpm i
node regen_font.mjs
```

脚本会：

1. 读取 `scripts/fonts/VonwaonBitmap-12px.ttf` 的全部字形
2. 使用 `lv_font_conv`：`--size 12 --bpp 1`，输出 LVGL 格式
3. 设置 `--lv-fallback lv_font_montserrat_14`（LVGL 图标符号）
4. 修正 `#include` 为 ESP-IDF 可用的 `lvgl.h`

## 更新源字体

替换 `scripts/fonts/VonwaonBitmap-12px.ttf` 后重新运行 `gen_ui_font.sh` 即可。

当前字体来源：

| 文件 | 来源 | 许可 |
|------|------|------|
| `VonwaonBitmap-12px.ttf` | npm `@fontpkg/vonwaon-bitmap-12px` / [VonwaonBitmap](https://github.com/wixette/VonwaonBitmap) | CC0 |

若本地缺失 TTF，可从 npm 包恢复：

```bash
cd launcher/scripts/fonts
npm pack @fontpkg/vonwaon-bitmap-12px
tar -xzf fontpkg-vonwaon-bitmap-12px-*.tgz
mv package/VonwaonBitmap-12px.ttf .
rm -rf package fontpkg-vonwaon-bitmap-12px-*.tgz
```

## 集成说明

- `launcher/main/ui_font.c`：`ui_font_default()` 返回 `ui_font_vonwaon`
- `launcher/main/CMakeLists.txt`：编译 `ui_font.c` 与 `fonts/ui_font_vonwaon.c`
- 文件管理器行高 `FM_ROW_H` 按 Vonwaon 约 13px 行高 + 内边距设置

## 字符覆盖

Vonwaon 含常用简体、日文假名等约 **7500+** 字形；**不含**完整繁体。缺失字符会尝试 Montserrat fallback（主要为拉丁与图标，CJK 仍可能显示为方块）。
