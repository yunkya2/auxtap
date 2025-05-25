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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <x68k/iocs.h>
#include <x68k/dos.h>
#include "auxtap.h"

//****************************************************************************
// for debugging
//****************************************************************************

#ifdef DEBUG
void DPRINTF(char *fmt, ...)
{
  char buf[256];
  va_list ap;

  va_start(ap, fmt);
  vsiprintf(buf, fmt, ap);
  va_end(ap);
#ifndef DEBUG_UART
  _iocs_b_print(buf);
#else
  char *p = buf;
  while (*p) {
    if (*p == '\n') {
      while (_iocs_osns232c() == 0)
        ;
      _iocs_out232c('\r');
    }
    while (_iocs_osns232c() == 0)
      ;
    _iocs_out232c(*p++);
  }
#endif
}
#else
#define DPRINTF(...)
#endif

//****************************************************************************
// 常駐部
//****************************************************************************

__asm__(
    ".long   d\n"        // データ領域へのポインタ
    ".ascii  \"" AUXTAP_SIGNATURE "\"\n" // 常駐チェック用

// SCC受信割り込み処理
// 旧ベクタ実行した後にauxintr_next_asmを呼ぶ

    ".global auxintr_asm\n"
"auxintr_asm:\n"
    "tst.b   0xcbc.w\n"                 // CPU type
    "beq     auxintr_asm_68000\n"
"auxintr_asm_68010:\n"                  // for 68010-
    "move.w  %sp@(6),%sp@-\n"           // frame typeもスタックに積む
"auxintr_asm_68000:\n"                  // for 68000
    "pea.l   %pc@(auxintr_next_asm)\n"  // saved PC
    "move.w  %sr,%sp@-\n"               // saved SR
    "move.l  %pc@(org_auxintr),%sp@-\n"
    "rts\n"                             // SCC受信処理後、auxintr_next_asmに飛ぶ


"auxintr_next_asm:\n"
    "movem.l %d0-%d2/%a0-%a3,%sp@-\n"
    "movea.l %pc@(paste_wptr),%a1\n"    // a1 = d.paste_wptr;
    "move.l  0x400+(0x32*4),%a2\n"      // a2 = IOCS _INP232C
    "move.l  0x400+(0x33*4),%a3\n"      // a3 = IOCS _ISNS232C
    "bra     7f\n"

"2:\n"
    "jsr     %a2@\n"                    // d0 = _iocs_inp232c();
    "tst.b   %d0\n"
    "bne     3f\n"
    "moveq.l #-0x80,%d0\n"              // d0 = (d0 == 0) ? 0x80 : d;

"3:\n"
    "tst.b   ispaste\n"                 // if (d.ispaste && d0 == 0x1b) {
    "beq     5f\n"                      //     // ペーストモード中にESCが押されたら
    "cmp.b   #0x1b,%d0\n"               //     // ペーストモードを終了する
    "bne     5f\n"
    "sf      ispaste\n"                 //     d.ispaste = false;
    "sf      issjis1\n"                 //     d.issjis1 = false;
    "move.l  paste_wptr,paste_rptr\n"   //     d.paste_rptr = d.paste_wptr;
    "bra     9f\n"                      //     return;
                                        // }
"5:\n"
    "move.b  %d0,%a1@+\n"               // *a1++ = d0;
    "cmpa.l  %pc@(paste_buf_end),%a1\n" // if (a1 >= d.paste_buf_end) {
    "bcs     6f\n"                      //     a1 = d.paste_buf;
    "movea.l %pc@(paste_buf),%a1\n"     // }
"6:\n"
    "cmpa.l  %pc@(paste_rptr),%a1\n"    // if (a1 != d.paste_rptr) {
    "beq     7f\n"                      //     d.paste_wptr = a1;
    "move.l  %a1,paste_wptr\n"          // }

"7:\n"
    "jsr     %a3@\n"                    // _iocs_isns232c()
    "tst.l   %d0\n"
    "bne     2b\n"

    "st.b    update\n"                  // d.update = true;
    "move.b  %pc@(inintr),%d0\n"
    "or.b    %pc@(ispaste),%d0\n"
    "bne     9f\n"                      // 多重割り込み中 or ペーストモードならバッファに格納するのみ

    "move.w  %sp@(7*4),%d0\n"           // 多重割り込み用に割り込み発生前の割り込みレベルを取得
    "ori.w   #0x2000,%d0\n"             // supervisor mode
    "move.l  %d0,%sp@-\n"               // if (!(d.inintr || d.ispaste)) {
    "bsr     auxintr\n"                 //     auxintr(sr);
    "addq.l  #4,%sp\n"                  // }

"9:\n"
    "movem.l %sp@+,%d0-%d2/%a0-%a3\n"
    "rte\n"

// Timer-C割り込み処理

    ".global timintr_asm\n"
"timintr_asm:\n"
    "movem.l %d0-%d2/%a0-%a2,%sp@-\n"
    "bsr     timintr\n"
    "movem.l %sp@+,%d0-%d2/%a0-%a2\n"
    "move.l  %pc@(org_timintr),%sp@-\n"
    "rts\n"

// キー入力関連IOCS処理

    ".global b_keyinp_asm\n"
"b_keyinp_asm:\n"
    "movem.l %d1-%d2/%a0-%a2,%sp@-\n"
    "bsr     b_keyinp\n"
    "movem.l %sp@+,%d1-%d2/%a0-%a2\n"
    "rts\n"

    ".global b_keysns_asm\n"
"b_keysns_asm:\n"
    "movem.l %d1-%d2/%a0-%a2,%sp@-\n"
    "bsr     b_keysns\n"
    "movem.l %sp@+,%d1-%d2/%a0-%a2\n"
    "rts\n"

    ".global key_init_asm\n"
"key_init_asm:\n"
    "sf      ispaste\n"
    "sf      issjis1\n"
    "move.l  paste_wptr,paste_rptr\n"
    "move.l  %pc@(org_key_init),%sp@-\n"
    "rts\n"

// 文字表示関連IOCS処理

    ".global b_putc_asm\n"
"b_putc_asm:\n"
    "movem.l %d1-%d2/%a0-%a2,%sp@-\n"
    "ext.l   %d1\n"
    "move.l  %d1,%sp@-\n"
    "bsr     b_putc\n"
    "addq.l  #4,%sp\n"
    "movem.l %sp@+,%d1-%d2/%a0-%a2\n"
    "rts\n"

    ".global b_print_asm\n"
"b_print_asm:\n"
    "move.l  %d1,%sp@-\n"
"1:\n"
    "clr.w   %d1\n"
    "move.b  %a1@+,%d1\n"
    "beq     2f\n"
    "bsr     b_putc_asm\n"
    "bra 1b\n"
"2:\n"
    "move.l  0x974,%d0\n"
    "movem.l %sp@+,%d1\n"
    "rts\n"
);

//----------------------------------------------------------------------------
// 文字出力関連処理
//----------------------------------------------------------------------------

static void out232c(int c)
{
    __asm__ volatile (
        // RS-232Cに出力が可能になるまで待つ
    "1:\n"
        "move.l  0x400+(0x34*4),%%a0\n"     // IOCS _OSNS232C
        "jsr     %%a0@\n"
        "tst.l   %%d0\n"
        "beq     1b\n"

        // RS-232Cに出力する
        "move.l  %0,%%d1\n"
        "move.l  0x400+(0x35*4),%%a0\n"     // IOCS _OUT232C
        "jsr     %%a0@\n"
        : : "d"(c) : "%%d0", "%%d1", "%%a0", "memory"
    );
}

int b_putc(int c)
{
    // 2バイト文字の場合は1バイトずつ出力
    if (c >= 0x100) {
        b_putc(c >> 8);
        return b_putc(c & 0xff);
    }

    if ((d.cursorx >= 0 && (d.cursorx != *(int16_t *)0x974)) ||
        (d.cursory >= 0 && (d.cursory != *(int16_t *)0x976))) {
        // 前回の出力からカーソル位置が変わっていたら改行する
        out232c('\r');
        out232c('\n');
    }

    // 表示可能な文字コードであればRS-232Cにも出力する
    uint8_t first = *(volatile uint8_t *)0x990;     // シフトJIS 1バイト目
    if (first < 0x80) {
        if (c == '\a' || c == '\b' || c == '\t' || c == '\n' || c == '\r' ||
            c == '\f' || c == '\v' || c == 0x1b ||
            ((c >= 0x20 && c <= 0xef) && (c != 0x80))) {
            out232c(c);
        }
    } else {
        if (((first >= 0x81 && first <= 0x9f) || (first >= 0xe0 && first <= 0xef)) &&
            ((c >= 0x40 && c <= 0xfc) && (c != 0x7f))) {
            out232c(c);
        }
    }

    // オリジナルのIOCS _B_PUTCを呼び出し後、カーソル位置を保存する
    __asm__ volatile (
        "move.l  %0,%%d1\n"
        "jsr     %1@\n"
        "move.l  %%d0,cursorx\n"
        : : "d"(c), "a"(d.org_b_putc) : "%%d0", "%%d1", "%%a0", "memory"
    );
}

// コンソールのAUX出力の有効・無効を切り替える
void conout_switch(int enable)
{
    if (enable) {
        d.cursorx = d.cursorx = -1;     // 前回のカーソル位置を無効化
        *(volatile uint32_t *)(0x400 + 0x20 * 4) = (int)b_putc_asm;
        *(volatile uint32_t *)(0x400 + 0x21 * 4) = (int)b_print_asm;
    } else {
        *(volatile uint32_t *)(0x400 + 0x20 * 4) = d.org_b_putc;
        *(volatile uint32_t *)(0x400 + 0x21 * 4) = d.org_b_print;
    }
}

//----------------------------------------------------------------------------
// キー入力関連処理
//----------------------------------------------------------------------------

int b_keyinp(void)
{
    uint32_t res;

    while (1) {
        // キーバッファに入力があればそれを返す
        __asm__ volatile (
            "move.l %1,%%a0\n"
            "jsr %%a0@\n"
            "move.l %%d0,%0\n"
            : "=d"(res) : "a"(d.org_b_keysns) : "%%d0", "%%a0", "memory"
        );
        if (res) {
            __asm__ volatile (
                "move.l %1,%%a0\n"
                "jsr %%a0@\n"
                "move.l %%d0,%0\n"
                : "=d"(res) : "a"(d.org_b_keyinp) : "%%d0", "%%a0", "memory"
            );
            return res;
        }

        if (!d.ispaste) {
            continue;
        }

        uint16_t stat = save_irq();
        if (d.paste_rptr != d.paste_wptr) {
            // ペーストバッファの内容を返す
            res = *d.paste_rptr;
            if (d.issjis1) {
                d.issjis1 = false;
            } else {
                if ((res >= 0x81 && res <= 0x9f) || (res >= 0xe0)) {
                    d.issjis1 = true;
                }
            }

            // 読み込みポインタを先に進める
            d.paste_rptr++;
            if (d.paste_rptr >= d.paste_buf_end) {
                d.paste_rptr = d.paste_buf;
            }

            // ペーストバッファが空になったらペーストモード終了
            if (d.paste_rptr == d.paste_wptr && !d.issjis1) {
                d.ispaste = false;
            }
            restore_irq(stat);
            return res;
        }
        restore_irq(stat);
    }
}

int b_keysns(void)
{
    uint32_t res;

    // オリジナルのIOCS _B_KEYSNSでキーバッファに入力があるかどうかをチェックする
    __asm__ volatile (
        "move.l %1,%%a0\n"
        "jsr %%a0@\n"
        "move.l %%d0,%0\n"
        : "=d"(res) : "a"(d.org_b_keysns) : "%%d0", "%%a0", "memory"
    );
    if (res || !d.ispaste) {
        return res;
    }

    // ペーストモードならペーストバッファが空でないかもチェックする
    uint16_t stat = save_irq();
    if (d.paste_rptr != d.paste_wptr) {
        res = 0x10000 | *d.paste_rptr;
    } else {
        res = 0;
    }
    restore_irq(stat);
    return res;
}

//----------------------------------------------------------------------------
// キーコード関連処理
//----------------------------------------------------------------------------

// IOCS _SKEYSET呼び出し
static inline void skeyset(uint16_t scancode)
{
    __asm__ volatile (
        "move.l     %0,%%d1\n"
        "move.l     0x400+(0x05*4),%%a0\n"  // IOCS _SKEYSET
        "jsr        %%a0@\n"
        : : "d"(scancode) : "%%d0", "%%d1", "%%a0", "memory"
    );
}

// キーコードシーケンスを送出
static int sendkey(uint8_t *k)
{
    uint8_t c;
    uint16_t stat = save_irq();

    // 送出するシーケンスのキーバッファ消費量を調べる
    int count = d.relptr;
    for (uint8_t *p = k; (c = *p) != '\0'; p++) {
        count += (c & 0x80) ? 2 : 1;    // シフト系キーは押す・離すの2回分
    }

    // シーケンスがキーバッファに入りきらなければエラー終了
    if (64 - *(uint16_t *)0x0812 < count) {
        restore_irq(stat);
        return -1;
    }

    // 前回送出したキーコードで押されたままになっているキーを離す
    while (d.relptr > 0) {
        skeyset(d.relseq[--d.relptr]);
    }

    switch (*k) {
    // 特殊キーコードの処理
    case KEYCODE_CON:
        d.isconout = true;   // コンソールAUX出力フラグを立てる
        conout_switch(true);
        break;
    case KEYCODE_COFF:
        d.isconout = false;  // コンソールAUX出力フラグを下げる
        conout_switch(false);
        break;
    case KEYCODE_CSW:
        d.isconout = !d.isconout; // コンソールAUX出力フラグを反転
        conout_switch(d.isconout);
        break;

    // 通常キーコードの処理
    default:
        while ((c = *k++) != 0) {
            if (c & 0x80) {         // シフト系キーの処理
                skeyset(c & 0x7f);      // キーを押す
                d.relseq[d.relptr++] = c;   // 後で離すキーコードを保存
            } else {
                skeyset(c);             // キーを押す
                skeyset(c | 0x80);      // キーを離す
            }
        }
        d.timcount = 2;  // シフト系キーを離すまでの時間を設定
        break;
    }

    restore_irq(stat);
    return 0;
}

// 前回送出したキーコードで押されたままになっているシフト系キーを離す
static void relkey(void)
{
    uint16_t stat = save_irq();
    while (d.relptr > 0) {
        skeyset(d.relseq[--d.relptr]);
    }
    d.timcount = 0;  // シフト系キーを離すまでの時間をリセット
    restore_irq(stat);
}

// Timer-C割り込み処理
void timintr(void)
{
    uint16_t status = save_irq();
    if (d.timcount > 0) {
        d.timcount--;
        if (d.timcount == 0) {
            relkey();   // シフト系キーを離す
        }
    }
    restore_irq(status);
}

// 入力文字列からキーコード列を探す
// IN:
//  key:  入力文字列
//  klen: 入力長
//  part: 1なら追加入力を考慮する, 0なら考慮しない
//  len:  !=NULL ならマッチした長さを返す
// OUT:
//  NULL:   マッチしない
//  -1:     部分マッチした
// その他:  マッチしたキーコード列へのポインタ

static uint8_t *keymatch(uint8_t *key, int klen, int part, int *len)
{
    uint8_t *m = d.keymap;
    int mlen = 0;
    uint8_t *res = NULL;

    // keymapのエントリを順に調べる
    while (*m != '\0') {
        uint8_t *k = key;
        int kl = klen;

        // エントリの文字列と入力文字列を比較する
        while (kl > 0) {
            if (*m != *k) {
                break;
            }
            m++;
            k++;
            kl--;
        }

        if (part) {     // 入力が更に続く可能性がある
            if (kl == 0) {      // ここまでの入力とはマッチした
                if (*m == '\0') {
                    if (mlen < klen) {
                        // マッチした長さが長い方を採用
                        mlen = klen;    // マッチした長さ
                        res = m + 1;    // 出力するキーコード列
                    }
                } else {
                    // 今後の入力でより長くマッチする可能性がある
                    return (uint8_t *)-1;
                }
            }
        } else {        // これ以上の入力は考慮しなくてよい
            if (*m == '\0') {
                if (mlen < klen - kl) {
                    // マッチした長さが長い方を採用
                    mlen = klen - kl;   // マッチした長さ
                    res = m + 1;        // 出力するキーコード列
                }
            }
        }

        // 次のエントリに進む
        while (*m++ != '\0') {
            ;   // 入力文字列をスキップ
        }
        while (*m++ != '\0') {
            ;   // 出力キーコード列をスキップ
        }
    }

    if (len) {
        *len = mlen;    // マッチした長さを返す
    }
    return res;
}

//----------------------------------------------------------------------------
// SCC受信割り込み処理
//----------------------------------------------------------------------------

void auxintr(uint32_t stat)
{
    d.inintr = true;
    do {
        d.update = false;

        uint8_t keyseq[8];
        int keyseqlen = 0;

        // シリアル入力をキーコード変換用バッファに移す
        uint8_t *p = d.paste_rptr;
        while (p != d.paste_wptr) {
            uint8_t c = *p++;
            if (p >= d.paste_buf_end) {
                p = d.paste_buf;
            }

            keyseq[keyseqlen++] = c;
            if (c > 0x80 || keyseqlen > sizeof(keyseq)) {
                // 変換用バッファに入りきらないか、2バイト文字コードの場合はペーストモードに移行
                relkey();
                d.ispaste = true;
                d.inintr = false;
                return;
            }
        }
        if (d.ispaste) {
            break;
        }

        restore_irq(stat);  // 処理中の多重割り込みを許可

        #ifdef DEBUG
        DPRINTF("{%d:", keyseqlen);
        for (int i = 0 ; i < keyseqlen; i++) DPRINTF("%02x ", keyseq[i]);
        DPRINTF("->");
        #endif

        // ここまでの入力文字列でマッチするものを探す
        uint8_t *keycode = keymatch(keyseq, keyseqlen, 1, NULL);

        #ifdef DEBUG
        if (keycode == NULL) {
            DPRINTF(" (no match)");
        } else if (keycode == (uint8_t *)-1) {
            DPRINTF(" (partial)");
        } else {
            for (uint8_t *k = keycode; *k; k++) DPRINTF(" %02x", *k);
        }
        #endif

        if (keycode == NULL) {
            // マッチしない
            uint8_t *key = keyseq;
            int kl = keyseqlen;
            while (kl > 0) {
                int len;
                // マッチしない場合は最長部分マッチを探す
                keycode = keymatch(key, kl, 0, &len);
                if (keycode == NULL) {
                    break;  // マッチするものがないので入力を無視
                } else {    // 先頭からマッチする部分のみを出力
                    #ifdef DEBUG
                    for (uint8_t *k = keycode; *k; k++) DPRINTF(" %02x", *k);
                    #endif
                    if (sendkey(keycode) < 0) {
                        // キーバッファに入りきらないのでペーストモードへ
                        stat = save_irq();
                        relkey();
                        d.ispaste = true;
                        d.inintr = false;
                        return;
                    }
                    // 出力した文字列の続きを調べる
                    key += len;
                    kl -= len;
                }
            }
        } else if (keycode == (uint8_t *)-1) {
            // 部分マッチ (後続の入力を待つ)
            keyseqlen = 0;
        } else {
            // 完全マッチ
            if (sendkey(keycode) < 0) {
                // キーバッファに入りきらないのでペーストモードへ
                stat = save_irq();
                relkey();
                d.ispaste = true;
                d.inintr = false;
                return;
            }
        }

        #ifdef DEBUG
        DPRINTF("}\r\n");
        #endif

        stat = save_irq();
        // キーバッファに入れた分だけ入力バッファのポインタを進める
        while (keyseqlen-- > 0) {
            d.paste_rptr++;
            if (d.paste_rptr >= d.paste_buf_end) {
                d.paste_rptr = d.paste_buf;
            }
        }
    } while (d.update);
    d.inintr = false;
}
