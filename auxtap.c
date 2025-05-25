/*
 * Copyright (c) 2024 Yuichi Nakamura (@yunkya2)
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
#include <string.h>
#include <ctype.h>
#include <x68k/iocs.h>
#include <x68k/dos.h>

extern uint32_t oldvect;
extern void auxintr_asm();

extern uint16_t keyptr;
extern uint8_t keyseq[16];

extern uint8_t keymap[4096];

__asm__(
"start: .long _start\n"
"oldvect: .long 0\n"
".ascii \"AXT\\0\"\n"

"auxintr_asm:\n"
"movem.l %d0-%d7/%a0-%a6,%sp@-\n"
"move.w %sr,%sp@-\n"
"ori.w #0x0700,%sr\n"
"bsr auxintr\n"
"move.w %sp@+,%sr\n"
"movem.l %sp@+,%d0-%d7/%a0-%a6\n"
"rte\n"

"keyptr: .space 2\n"
"keyseq: .space 16\n"
".even\n"
);

static void skeyset(uint16_t scancode)
{
    __asm__ volatile (
        "move.l %0,%%d1\n"
        "moveq.l #0x05,%%d0\n"
        "trap #15\n"
        : : "d"(scancode) : "%%d0", "%%d1"
    );
}

static void sendkey(uint8_t *keycode)
{
    uint8_t *k = keycode;
    while (*k != 0) {
        skeyset(*k++);
    }
    while (--k >= keycode) {
        skeyset(*k | 0x80);
    }
}

static uint8_t *keymatch(uint8_t *key, int part, int *len)
{
    uint8_t *m = keymap;
    int mlen = 0;
    uint8_t *res = NULL;

//    printf("--\n");
    while (*m != '\0') {
        uint8_t *k = key;
//     printf("%02x\n", *m);

        while (*m == *k) {
            if (*m == '\0') {
                if (mlen < k - key) {
                    // マッチした長さが長い方を採用
                    mlen = k - key;        // マッチした長さ
                    res = m + 1;           // 出力するキーコード列
                    break;
                }
            }
            m++;
            k++;
        }

        if (*m != *k) {
            // 途中までマッチする場合、もっと長いシーケンスが来る可能性がある
            if (*k == '\0' && part) {
                return (uint8_t *)-1;
            }

            // 完全マッチしない場合は、最も長い部分マッチを探す
            if (*m == '\0' && !part) {
                if (mlen < k - key) {
                    // マッチした長さが長い方を採用
                    mlen = k - key;        // マッチした長さ
                    res = m + 1;           // 出力するキーコード列
                }
            }
        }

        // 次の入力列へ進む
        while (*m++ != '\0') {
            ;
        }
        while (*m++ != '\0') {
            ;
        }
    }

    if (len) {
        *len = mlen;    // マッチした長さを返す
    }
    return res;
}

void auxintr(void)
{
    uint8_t data = *(volatile uint16_t *)0xe98006 & 0xff; // SCC data port

#ifdef DEBUG
    _iocs_b_putc('{');
    _iocs_b_putc("0123456789abcdef"[(data >> 4) & 0xf]);
    _iocs_b_putc("0123456789abcdef"[(data >> 0) & 0xf]);
    _iocs_b_putc('}');
#endif

    keyseq[keyptr++] = data;
    keyseq[keyptr] = 0;

    uint8_t *keycode = keymatch(keyseq, 1, NULL);

    if (keycode == NULL) {
        // マッチしない
        while (keyptr > 0) {
            int len;
            // マッチしない場合は最長部分マッチを探す
            keycode = keymatch(keyseq, 0, &len);
            if (keycode == NULL) {
                // 部分マッチもしない
                for (int i = 0; i < keyptr; i++) {
                    _iocs_b_putc('{');
                    _iocs_b_putc("0123456789abcdef"[(data >> 4) & 0xf]);
                    _iocs_b_putc("0123456789abcdef"[(data >> 0) & 0xf]);
                    _iocs_b_putc('}');
                }
                keyptr = 0;
            } else {
                sendkey(keycode);
                uint8_t *k = &keyseq[len];
                keyptr = 0;
                while (*k != '\0') {
                    *k++ = keyseq[keyptr++];
                }
                keyptr = k - keyseq;
            }
        }
    } else if (keycode == (uint8_t *)-1) {
        // 部分マッチ
    } else {
        // 完全マッチ
        sendkey(keycode);
        keyptr = 0;
    }

//    __asm__ volatile ("tst.b 0xe9a001\n");
//    __asm__ volatile ("tst.b 0xe9a001\n");
    *(volatile uint16_t *)0xe98004 = 0x0038; // SCC command port
}


__asm__(
    "keymap: .space 4096\n"
    ".even\n"   
);

///////////////////////////////////////////////////////////////////// ここまでが常駐部

void help(void);

int main(int argc, char **argv)
{

    FILE *fp;

    if ((fp = fopen("auxtap.cnf", "r")) == NULL) {
        _dos_print("auxtap.cnfが見つかりません\r\n");
        exit(1);
    }


    char line[256];
    uint8_t *mp = keymap;

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;
        uint8_t *m = mp;
        uint8_t c;

        while (isspace(*p)) {
            p++;
        }
        if (*p == '#') {
            continue;
        }

        c = 0;
        while (!isspace(*p) && *p != '\0') {
            if (*p == '\\') {
                p++;
                if (*p == 'x') {
                    p++;
                    c = (uint8_t)strtol(p, &p, 16);
                } else {
                    c = (uint8_t)strtol(p, &p, 0);
                }
                if (c == 0) {
                    break;
                }
                *m++ = c;
            } else {
                c = *p++;
                *m++ = c;
            }
        }
        *m++ = '\0';
        if (c == 0) {
            continue;
        }

        do {
            c = (uint8_t)strtol(p, &p, 16);
            if (c == 0) {
                break;
            }
            *m++ = c;
            while (isspace(*p)) {
                p++;
            }
        } while (*p++ == ',');
        *m++ = '\0';
        if (c == 0) {
            continue;
        }

        mp = m;
    }
    fclose(fp);
    *mp = '\0';

    #if 0
    int i = 0;
    for (uint8_t *p = keymap; p != mp; p++, i++) {
        printf("%02x ", *p);
        if ((i & 0x0f) == 0x0f) {
            printf("\n");
        }
    }
    printf("\n%d\n", mp - keymap);
    #endif

#if 0
    int l;
    printf("%p\n", keymatch((uint8_t *)"\x1b[D", 1, NULL));
    printf("%02x\n", *keymatch((uint8_t *)"\x1b[D", 1, NULL));
    printf("%p\n", keymatch((uint8_t *)"\x1b[", 1, NULL));
    printf("%p\n", keymatch((uint8_t *)"\x1b(", 1, NULL));
    printf("\n");
    printf("%p\n", keymatch((uint8_t *)"\x41", 1, NULL));

    printf("\n");
    printf("%p\n", keymatch((uint8_t *)"\x1b[", 0, NULL));
    printf("%02x\n", *keymatch((uint8_t *)"\x1b[", 0, &l));
    printf("%d\n\n", l);

    printf("%p\n", keymatch((uint8_t *)"\x1b", 0, NULL));
    printf("%02x\n", *keymatch((uint8_t *)"\x1b", 0, &l));
    printf("%d\n", l);
#endif

//    exit(0);

    _dos_print("AUX tap driver for X680x0 version " GIT_REPO_VERSION "\r\n");

    int baudrate = 0;
    int bdset = -1;
    int release = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '/' || argv[i][0] =='-') {
            switch (argv[i][1]) {
            case 's':
                baudrate = atoi(&argv[i][2]);
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
        bdset = 9;
        static const int bauddef[] = { 75, 150, 300, 600, 1200, 2400, 4800, 9600, 19200, 38400 };
        for (int i = 0; i < sizeof(bauddef) / sizeof(bauddef[0]); i++) {
            if (baudrate == bauddef[i]) {
            bdset = i;
            break;
            }
        }
    }
    if (bdset > 0) {
        // stop 1 / nonparity / 8bit / nonxoff
        _iocs_set232c(0x4c00 | bdset);
    }

    _iocs_b_super(0);
    uint32_t oldvect = *(volatile uint32_t *)(0x5c * 4);

    if (release) {
        if (strcmp((char *)(oldvect - 4), "AXT") != 0) {
            _dos_print("auxtapが常駐していません\r\n");
            exit(1);
        }

        *(volatile uint32_t *)(0x5c * 4) = *(volatile uint32_t *)(oldvect - 8);
        _dos_mfree((void *)(*(volatile uint32_t *)(oldvect - 12) - 0xf0));
        _dos_print("auxtapの常駐を解除しました\r\n");
        exit(0);
    }

    if (strcmp((char *)(oldvect - 4), "AXT") == 0) {
        _dos_print("auxtapが既に常駐しています\r\n");
        exit(1);
    }

    *(volatile uint32_t *)(0x5c * 4) = (int)auxintr_asm;

    extern void _start();
    int size = (int)main - (int)_start;
#ifdef DEBUG
    int size = 0xffffffff;
#endif
    _dos_allclose();
    _dos_print("auxtapが常駐しました\r\n");
    _dos_keeppr(size, 0);
}

void help(void)
{
    _dos_print("AUX1からの入力をキー入力として扱います\r\n");
    _dos_print("Usage: auxtap.x [-r][-s<baud>]\r\n");
    exit(1);
}
