#!/usr/bin/env node
// Post-processes an lv_font_conv C file (gen_fonts.sh).
//   node patch_font.cjs <file.c> <line_height> [tabular]
// line_height: the line height of design-system 3; the extra space is split
// evenly above and below the glyphs by moving the baseline.
// tabular: every digit 0-9 gets the widest digit's advance, its glyph box is
// centred in it, and digits drop out of kerning.
'use strict';
const fs = require('fs');

const [file, lineArg, mode] = process.argv.slice(2);
const lineHeight = Number(lineArg);
if (!file || !Number.isInteger(lineHeight)) {
    console.error('usage: patch_font.cjs <file.c> <line_height> [tabular]');
    process.exit(2);
}
let src = fs.readFileSync(file, 'utf8');

const lh = /\.line_height = (\d+),/.exec(src);
const bl = /\.base_line = (-?\d+),/.exec(src);
const grow = lineHeight - Number(lh[1]);
if (grow < 0) {
    console.error(`${file}: glyphs need ${lh[1]} px, more than the line height ${lineHeight}`);
    process.exit(1);
}
src = src.replace(lh[0], `.line_height = ${lineHeight},`)
         .replace(bl[0], `.base_line = ${Number(bl[1]) + Math.floor(grow / 2)},`);

if (mode === 'tabular') {
    // Glyph ids follow the order of the "U+XXXX" comments in the bitmap table, starting at 1.
    const codes = [...src.matchAll(/\/\* U\+([0-9A-F]+) /g)].map((m) => parseInt(m[1], 16));
    const digitIds = new Set();
    codes.forEach((c, i) => { if (c >= 0x30 && c <= 0x39) digitIds.add(i + 1); });

    const start = src.indexOf('glyph_dsc[] = {');
    const end = src.indexOf('};', start);
    const lines = src.slice(start, end).split('\n');
    const re = /\.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+)/;
    let id = -1;
    const entries = [];
    lines.forEach((line, n) => {
        if (line.includes('.bitmap_index')) {
            id++;
            if (digitIds.has(id)) entries.push({ n, m: re.exec(line) });
        }
    });
    const adv = Math.max(...entries.map((e) => Number(e.m[1])));
    for (const { n, m } of entries) {
        const boxW = Number(m[2]);
        const ofsX = Math.round(((adv / 16) - boxW) / 2);
        lines[n] = lines[n].replace(m[0], `.adv_w = ${adv}, .box_w = ${boxW}, .box_h = ${m[3]}, .ofs_x = ${ofsX}`);
    }
    src = src.slice(0, start) + lines.join('\n') + src.slice(end);

    for (const table of ['kern_left_class_mapping', 'kern_right_class_mapping']) {
        const s = src.indexOf(`${table}[] =`);
        if (s < 0) continue;
        const open = src.indexOf('{', s);
        const close = src.indexOf('}', open);
        let k = 0;
        const body = src.slice(open + 1, close).replace(/\d+/g, (v) => (digitIds.has(k++) ? '0' : v));
        src = src.slice(0, open + 1) + body + src.slice(close);
    }
    if (src.includes('kern_pair_glyph_ids')) {
        console.error(`${file}: pair kerning is not handled; regenerate with --force-fast-kern-format`);
        process.exit(1);
    }
}
fs.writeFileSync(file, src);
