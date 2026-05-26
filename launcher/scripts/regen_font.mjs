#!/usr/bin/env node

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { createRequire } from 'module';
import opentype from 'opentype.js';

const require = createRequire(import.meta.url);
const lv_font_conv = require('lv_font_conv/lib/cli');

// ==================== 配置区域 ====================
const FONT_NAME = 'Cubic_11';        // 字体文件名（不含后缀）
const LV_FONT_NAME = 'ui_font_esplayfont';   // LVGL 内部的字体变量名
const FONT_SIZE = '12';                      // 字体渲染大小 (从 12 改为 16)
// =================================================

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const SCRIPT_DIR = __dirname;
const ROOT = path.resolve(SCRIPT_DIR, '..');
const FONT_PATH = path.join(SCRIPT_DIR, 'fonts', `${FONT_NAME}.ttf`);
const OUT_PATH = path.join(ROOT, 'main', 'fonts', `${LV_FONT_NAME}.c`);

// 1. 检查源字体文件是否存在
if (!fs.existsSync(FONT_PATH)) {
  console.error(`Missing source font: ${FONT_PATH}`);
  console.error(`See ${path.join(SCRIPT_DIR, 'FONT.md')}`);
  process.exit(1);
}

function toRanges(codes) {
  if (codes.length === 0) return [];

  const parts = [];
  let start = codes[0];
  let prev = codes[0];

  for (let i = 1; i < codes.length; i++) {
    const c = codes[i];
    if (c === prev + 1) {
      prev = c;
    } else {
      parts.push(start === prev ? `0x${start.toString(16)}` : `0x${start.toString(16)}-0x${prev.toString(16)}`);
      start = prev = c;
    }
  }
  parts.push(start === prev ? `0x${start.toString(16)}` : `0x${start.toString(16)}-0x${prev.toString(16)}`);
  return parts;
}

async function main() {
  try {
    const buffer = fs.readFileSync(FONT_PATH);
    const font = opentype.parse(buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength));

    const vonCodes = [];
    if (font.tables && font.tables.cmap && font.tables.cmap.glyphIndexMap) {
      const glyphMap = font.tables.cmap.glyphIndexMap;
      for (const unicodeStr in glyphMap) {
        const unicode = parseInt(unicodeStr, 10);
        if (!isNaN(unicode)) {
          vonCodes.push(unicode);
        }
      }
    } else {
      const numGlyphs = font.numGlyphs || (font.glyphs ? font.glyphs.length : 0);
      for (let i = 0; i < numGlyphs; i++) {
        const glyph = font.glyphs.get(i);
        if (glyph && glyph.unicode !== undefined && glyph.unicode !== null) {
          vonCodes.push(glyph.unicode);
        }
      }
    }

    if (vonCodes.length === 0) {
      throw new Error("No valid unicode glyphs found in the font file.");
    }

    vonCodes.sort((a, b) => a - b);
    const vonParts = toRanges(vonCodes);

    const chunkSize = 200;
    const chunks = [];
    for (let i = 0; i < vonParts.length; i += chunkSize) {
      chunks.push(vonParts.slice(i, i + chunkSize));
    }

    // 2. 将配置参数应用到命令构建中
    const args = [
      '--size', FONT_SIZE,
      '--bpp', '1',
      '--format', 'lvgl',
      '--font', FONT_PATH,
    ];

    for (const chunk of chunks) {
      args.push('-r', chunk.join(','));
    }

    args.push(
      '--lv-font-name', LV_FONT_NAME,
      '--lv-fallback', 'lv_font_montserrat_14',
      '--lv-include', 'lvgl.h',
      '-o', OUT_PATH
    );

    console.log(`${FONT_NAME} ${vonCodes.length} glyphs, ${chunks.length} range chunks`);

    // 3. 执行转换
    console.log(`Generating font, please wait...`);
    await lv_font_conv.run(args);

    // 4. 替换 include
    if (fs.existsSync(OUT_PATH)) {
      let content = fs.readFileSync(OUT_PATH, 'utf8');
      content = content.replace(/#include "lvgl\/lvgl\.h"/g, '#include "lvgl.h"');
      fs.writeFileSync(OUT_PATH, content, 'utf8');

      const stats = fs.statSync(OUT_PATH);
      console.log(`Wrote ${OUT_PATH} (${stats.size} bytes)`);
    } else {
      console.error(`Error: Output file was not generated at ${OUT_PATH}`);
    }

  } catch (error) {
    console.error('An error occurred during font regeneration:', error.message);
    process.exit(1);
  }
}

main();