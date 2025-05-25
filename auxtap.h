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

#ifndef _AUXTAP_H_
#define _AUXTAP_H_

//#define DEBUG

#define AUXTAP_SIGNATURE    "AXT2"  // 常駐部チェック用

// 特殊キーコード定義
#define KEYCODE_CON     0xff        // コンソールのAUX出力を有効にする
#define KEYCODE_COFF    0xfe        // コンソールのAUX出力を無効にする
#define KEYCODE_CSW     0xfd        // コンソールのAUX出力の有効・無効を切り替える

#include "auxtapdata.h"

extern struct data d;
extern void auxintr_asm();
extern void b_keyinp_asm();
extern void b_keysns_asm();
extern void key_init_asm();
extern void b_putc_asm();
extern void b_print_asm();

void conout_switch(int);

// 割り込み禁止状態にする
static inline uint16_t save_irq(void)
{
    uint16_t oldirq;
    __asm__ volatile (
        "move.w %%sr,%0\n"
        "ori.w  #0x0700,%%sr\n"
        : "=d"(oldirq) : : "memory"
    );
    return oldirq;
}

// 割り込み状態を元に戻す
static inline void restore_irq(uint16_t sr)
{
    __asm__ volatile (
        "move.w %0,%%sr\n"
        : : "d"(sr) : "memory"
    );
}

#endif /* _AUXTAP_H_ */
