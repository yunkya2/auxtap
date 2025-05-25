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
// 非常駐部
//****************************************************************************

void help(void)
{
    _dos_print("AUX1からの入力をキー入力として扱います\r\n");
    _dos_print("Usage: auxtap.x [-r][-s<baud>][-b<paste buffer size>]\r\n");
    exit(1);
}

uint8_t *readconfig(uint8_t *map, size_t mapsize)
{
    FILE *fp;
    char cfgname[256];
    extern struct dos_psp *_PSP;

    // 実行ファイルと同じパスから読む
    strcpy(cfgname, _PSP->exe_path);
    strcat(cfgname, "auxtap.cnf");
    if ((fp = fopen(cfgname, "r")) == NULL) {
        printf("auxtap.cnfが見つかりません\n");
        return NULL;
    }

    uint8_t *mp = map;
    uint8_t *me = map + mapsize - 1;
    char line[256];
    int linenum = 0;
    bool cfgerr = false;

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;
        uint8_t *m = mp;
        uint8_t c;
        linenum++;

        while (isspace(*p)) {
            p++;
        }
        if (*p == '#') {
            continue;       // コメント行
        }

        // 入力文字列を取得する
        c = 0;
        while (!isspace(*p) && *p != '\0') {
            if (*p == '\\') {       // エスケープ文字
                char *np;
                p++;
                if (*p == '\\') {           // \\
                    c = *p;
                    np = p + 1;
                } else if (*p == 'x') {     // \xab 
                    p++;
                    c = (uint8_t)strtol(p, &np, 16);
                } else {                    // \012
                    c = (uint8_t)strtol(p, &np, 0);
                }
                if (p == np) {
                    cfgerr = true;
                    break;
                }
                if (c == 0) {
                    c = 0x80;   // \x00 (CTRL+@) -> \x80に変換
                }
                p = np;
            } else {
                c = *p++;
            }
            *m++ = c;
        }
        *m++ = '\0';
        if (c == 0) {       // 1文字も取得できなかった場合
            if (cfgerr) {
                break;      // エラー
            } else {
                continue;   // 空行
            }
        }
        if (m >= me) {      // バッファオーバー
            cfgerr = true;
            break;
        }

        // 出力キーコード列を取得する
        bool getkey = false;
        while ((c = (uint8_t)strtol(p, &p, 16)) != 0) {
            getkey = true;
            if (*p == '+') {
                p++;
                *m++ = c | 0x80;
            } else {
                *m++ = c;
            }
        }
        *m++ = '\0';
        if (!getkey) {      // 1文字も取得できなかった場合
            cfgerr = true;
            break;
        }

        if (m >= me) {      // バッファオーバー
            cfgerr = true;
            break;
        }
        mp = m;             // 正しい行が得られたのでポインタを進める
    }
    fclose(fp);
    *mp++ = '\0';
    if (cfgerr) {
        printf("auxtap.cnfの%d行目でエラーが発生しました\n", linenum);
        return NULL;
    }

    return mp;
}

int main(int argc, char **argv)
{
    _dos_print("AUX tap driver for X680x0 version " GIT_REPO_VERSION "\r\n");

    int baudrate = 0;
    int pastebufsize = 65536;
    int release = 0;

    // コマンドライン引数の解析

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '/' || argv[i][0] =='-') {
            switch (argv[i][1]) {
            case 's':
                baudrate = atoi(&argv[i][2]);
                break;
            case 'b':
                pastebufsize = atoi(&argv[i][2]);
                break;
            case 'r':
                release = 1;
                break;
            default:
                help();
                break;
            }
        }
    }

    if (baudrate > 0) {
        int bdset = 7;
        static const int bauddef[] = { 75, 150, 300, 600, 1200, 2400, 4800, 9600, 19200, 38400 };
        for (int i = 0; i < sizeof(bauddef) / sizeof(bauddef[0]); i++) {
            if (baudrate == bauddef[i]) {
            bdset = i;
            break;
            }
        }
        // stop 1 / nonparity / 8bit / nonxoff
       _iocs_set232c(0x4c00 | bdset);
    }

    if (!release) {
        // 設定ファイルを読む
        if ((d.paste_buf = readconfig(d.keymap, sizeof(d.keymap))) == NULL) {
            printf("設定ファイルの読み込みに失敗しました\n");
            exit(1);
        }
    }

    d.paste_buf_end = d.paste_buf + pastebufsize;
    d.paste_wptr = d.paste_buf;
    d.paste_rptr = d.paste_buf;

    _iocs_b_super(0);
    uint32_t org_auxintr = *(volatile uint32_t *)(0x5c * 4);
    int exist = (memcmp((char *)(org_auxintr - 4), AUXTAP_SIGNATURE, 4) == 0);

    if (release) {
        if (!exist) {
            _dos_print("auxtapが常駐していません\r\n");
            exit(1);
        }

        struct data *dp = (struct data *)*(uint32_t *)(org_auxintr - 8);
        *(volatile uint32_t *)(0x5c * 4) = dp->org_auxintr;
        *(volatile uint32_t *)(0x400 + 0x00 * 4) = dp->org_b_keyinp;
        *(volatile uint32_t *)(0x400 + 0x01 * 4) = dp->org_b_keysns;
        *(volatile uint32_t *)(0x400 + 0x03 * 4) = dp->org_key_init;
        _dos_mfree((void *)dp->org_start);
        _dos_print("auxtapの常駐を解除しました\r\n");
        exit(0);
    } else {
        if (exist) {
            _dos_print("auxtapが既に常駐しています\r\n");
            exit(1);
        }

        extern uint32_t _MCB;
        d.org_start = _MCB + 0x10;
        d.org_auxintr = org_auxintr;
        d.org_b_keyinp = *(volatile uint32_t *)(0x400 + 0x00 * 4);
        d.org_b_keysns = *(volatile uint32_t *)(0x400 + 0x01 * 4);
        d.org_key_init = *(volatile uint32_t *)(0x400 + 0x03 * 4);

        *(volatile uint32_t *)(0x5c * 4) = (int)auxintr_asm;
        *(volatile uint32_t *)(0x400 + 0x00 * 4) = (int)b_keyinp_asm;
        *(volatile uint32_t *)(0x400 + 0x01 * 4) = (int)b_keysns_asm;
        *(volatile uint32_t *)(0x400 + 0x03 * 4) = (int)key_init_asm;
    }

#ifdef DEBUG
    int size = INT32_MAX; // (実際には現メモリブロック全体が常駐する)
#else
    extern void _start();
    int size = (int)d.paste_buf_end  - (int)_start;
#endif

    _dos_allclose();
    _dos_print("auxtapが常駐しました\r\n");
    _dos_keeppr(size, 0);
}
