/*
 * Copyright (c) 2024,2025 Yuichi Nakamura (@yunkya2)
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>

#ifndef _AUXTAPDATA_H_
#define _AUXTAPDATA_H_

#define RELBUFSIZE          16          // シフトキーバッファサイズ
#define KEYMAPSIZE          4096        // 入力変換テーブルのサイズ

#define STRING(str)      #str
#define STRING2(str)     STRING(str)

// 常駐部から利用するデータ領域 (__asm__ 定義と一致させること)

struct data {
    uint32_t org_start;             // 常駐部先頭アドレス
    uint32_t org_auxintr;           // SCC受信割り込み 変更前ベクタ

    uint32_t org_b_keyinp;          // IOCS _B_KEYINP 変更前ベクタ
    uint32_t org_b_keysns;          // IOCS _B_KEYSNS 変更前ベクタ
    uint32_t org_key_init;          // IOCS _KEY_INIT 変更前ベクタ
    uint32_t org_b_putc;            // IOCS _B_PUTC   変更前ベクタ
    uint32_t org_b_print;           // IOCS _B_PRINT  変更前ベクタ

    uint8_t *paste_buf;             // paste bufferアドレス
    uint8_t *paste_buf_end;         // paste buffer終了アドレス
    uint8_t *paste_wptr;            // paste buffer書き込みポインタ
    uint8_t *paste_rptr;            // paste buffer読み出しポインタ

    int16_t cursorx;                // _B_PUTC実行後のカーソルX座標
    int16_t cursory;                // _B_PUTC実行後のカーソルY座標

    uint8_t inintr;                 // 割り込みハンドラ実行中フラグ
    uint8_t ispaste;                // pasteモードフラグ
    uint8_t update;                 // 入力文字列更新フラグ
    uint8_t issjis1;                // SJIS1バイト目フラグ
    uint8_t isconout;               // コンソールAUX出力フラグ

    uint16_t relptr;                // シフトキーバッファ使用サイズ
    uint8_t relseq[RELBUFSIZE];     // シフトキーバッファ

    uint8_t keymap[KEYMAPSIZE];     // 入力文字列 -> キーコード変換テーブル
};

#ifdef GENDATA
__asm__(
    ".global    d\n"
"d:\n"
"org_start:     .space 4\n"
    ".global    org_auxintr\n"
"org_auxintr:   .space 4\n"

    ".global    org_b_keyinp\n"
    ".global    org_b_keysns\n"
    ".global    org_key_init\n"
    ".global    org_b_putc\n"
    ".global    org_b_print\n"
"org_b_keyinp:  .space 4\n"
"org_b_keysns:  .space 4\n"
"org_key_init:  .space 4\n"
"org_b_putc:    .space 4\n"
"org_b_print:   .space 4\n"

    ".global    paste_buf\n"
    ".global    paste_buf_end\n"
    ".global    paste_wptr\n"
    ".global    paste_rptr\n"
"paste_buf:     .space 4\n"
"paste_buf_end: .space 4\n"
"paste_wptr:    .space 4\n"
"paste_rptr:    .space 4\n"

    ".global    cursorx\n"
    ".global    cursory\n"
"cursorx:       .space 2\n"
"cursory:       .space 2\n"

    ".global    inintr\n"
    ".global    ispaste\n"
    ".global    update\n"
    ".global    issjis1\n"
    ".global    isconout\n"
"inintr:        .space 1\n"
"ispaste:       .space 1\n"
"update:        .space 1\n"
"issjis1:       .space 1\n"
"isconout:      .space 1\n"
    ".even\n"

"relptr:        .space 2\n"
"relseq:        .space " STRING2(RELBUFSIZE) "\n"
    ".even\n"

"keymap:        .space " STRING2(KEYMAPSIZE) "\n"
    ".even\n"
#ifdef DEBUG
    ".space     65536\n"  // for paste buffer
#endif
);
#endif

#endif /* _AUXTAPDATA_H_ */
