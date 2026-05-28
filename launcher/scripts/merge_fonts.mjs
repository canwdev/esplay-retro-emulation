#!/usr/bin/env node

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { createFont } from 'fonteditor-core';

// ==================== 配置区域 ====================
const PRIMARY_FONT = 'Cubic_11.ttf'; // 优先使用的点阵字体
const SECONDARY_FONT = 'VonwaonBitmap-12px.ttf';         // 补充缺失的字形
const OUTPUT_FONT = 'EsplayMerged-12px.ttf';   // 融合后的 TTF 输出文件名
// =================================================

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const SCRIPT_DIR = __dirname;
const FONTS_DIR = path.join(SCRIPT_DIR, 'fonts');
const PRIMARY_PATH = path.join(FONTS_DIR, PRIMARY_FONT);
const SECONDARY_PATH = path.join(FONTS_DIR, SECONDARY_FONT);
const OUTPUT_PATH = path.join(FONTS_DIR, OUTPUT_FONT);

function cloneGlyph(glyph) {
  return JSON.parse(JSON.stringify(glyph));
}

function isMergeableGlyph(glyph) {
  return glyph?.contours?.length > 0;
}

function mergeFonts(primaryPath, secondaryPath, outputPath) {
  const baseFont = createFont(fs.readFileSync(primaryPath), { type: 'ttf' });
  const supplementalFont = createFont(fs.readFileSync(secondaryPath), { type: 'ttf' });

  const base = baseFont.get();
  const supplemental = supplementalFont.get();
  const existingCodes = new Set(Object.keys(base.cmap).map(Number));

  let added = 0;
  let skippedOverlap = 0;
  let skippedEmpty = 0;

  for (const codeStr of Object.keys(supplemental.cmap)) {
    const code = Number(codeStr);
    if (existingCodes.has(code)) {
      skippedOverlap++;
      continue;
    }

    const glyphIndex = supplemental.cmap[code];
    const glyph = supplemental.glyf[glyphIndex];
    if (!isMergeableGlyph(glyph)) {
      skippedEmpty++;
      continue;
    }

    const copy = cloneGlyph(glyph);
    copy.unicode = [code];
    if (!copy.name) {
      copy.name = `uni${code.toString(16).toUpperCase()}`;
    }

    const newIndex = base.glyf.length;
    base.glyf.push(copy);
    base.cmap[code] = newIndex;
    existingCodes.add(code);
    added++;
  }

  baseFont.data = base;
  const outputBuffer = baseFont.write({ type: 'ttf' });
  fs.writeFileSync(outputPath, outputBuffer);

  return {
    primaryGlyphs: Object.keys(createFont(fs.readFileSync(primaryPath), { type: 'ttf' }).get().cmap).length,
    supplementalGlyphs: added,
    totalGlyphs: Object.keys(base.cmap).length,
    skippedOverlap,
    skippedEmpty,
    outputBytes: outputBuffer.length,
  };
}

function main() {
  for (const fontPath of [PRIMARY_PATH, SECONDARY_PATH]) {
    if (!fs.existsSync(fontPath)) {
      console.error(`Missing source font: ${fontPath}`);
      console.error(`See ${path.join(SCRIPT_DIR, 'FONT.md')}`);
      process.exit(1);
    }
  }

  try {
    const stats = mergeFonts(PRIMARY_PATH, SECONDARY_PATH, OUTPUT_PATH);

    console.log(`${PRIMARY_FONT}: ${stats.primaryGlyphs} glyphs (priority)`);
    console.log(`${SECONDARY_FONT}: ${stats.supplementalGlyphs} supplemental glyphs`);
    console.log(`Skipped overlap: ${stats.skippedOverlap}, skipped empty: ${stats.skippedEmpty}`);
    console.log(`Merged total: ${stats.totalGlyphs} glyphs`);
    console.log(`Wrote ${OUTPUT_PATH} (${stats.outputBytes} bytes)`);
  } catch (error) {
    console.error('Font merge failed:', error.message);
    process.exit(1);
  }
}

main();
