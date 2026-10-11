/*
 * Генератор QR-кодов (ISO/IEC 18004), только то, что нужно приложению:
 * байтовый режим, уровень коррекции M, версии 1..6, выбор маски по штрафам.
 */
#include <string.h>
#include <stdlib.h>
#include "qr.h"

#define MAXV 6

typedef struct { int total_cw, ec_per_block, blocks; } vinfo_t;
/* уровень M: всего кодовых слов, ECC-слов на блок, число блоков */
static const vinfo_t VT[MAXV + 1] = {
    {0, 0, 0}, {26, 10, 1}, {44, 16, 1}, {70, 26, 1}, {100, 18, 2}, {134, 24, 2}, {172, 16, 4}
};

/* ---------- GF(256) и Рид-Соломон ---------- */

static uint8_t gexp[512], glog[256];
static int gf_ready = 0;

static void gf_init(void) {
    if (gf_ready) return;
    int x = 1;
    for (int i = 0; i < 255; i++) {
        gexp[i] = (uint8_t)x;
        glog[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (int i = 255; i < 512; i++) gexp[i] = gexp[i - 255];
    gf_ready = 1;
}

static uint8_t gmul(uint8_t a, uint8_t b) {
    if (!a || !b) return 0;
    return gexp[glog[a] + glog[b]];
}

/* делитель (порождающий многочлен) степени deg, старший коэффициент (1) опущен */
static void rs_divisor(int deg, uint8_t *res) {
    memset(res, 0, deg);
    res[deg - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < deg; i++) {
        for (int j = 0; j < deg; j++) {
            res[j] = gmul(res[j], root);
            if (j + 1 < deg) res[j] ^= res[j + 1];
        }
        root = gmul(root, 2);
    }
}

static void rs_remainder(const uint8_t *data, int len, const uint8_t *div, int deg, uint8_t *res) {
    memset(res, 0, deg);
    for (int i = 0; i < len; i++) {
        uint8_t factor = data[i] ^ res[0];
        memmove(res, res + 1, deg - 1);
        res[deg - 1] = 0;
        for (int j = 0; j < deg; j++) res[j] ^= gmul(div[j], factor);
    }
}

/* ---------- матрица ---------- */

static int N;                                   /* сторона */
static uint8_t mod[QR_MAX_SIZE][QR_MAX_SIZE];   /* 1 = тёмный */
static uint8_t isf[QR_MAX_SIZE][QR_MAX_SIZE];   /* 1 = служебный модуль */

static void set_func(int x, int y, int dark) {
    if (x < 0 || y < 0 || x >= N || y >= N) return;
    mod[y][x] = (uint8_t)(dark ? 1 : 0);
    isf[y][x] = 1;
}

static int bit(unsigned v, int i) { return (v >> i) & 1; }

static void draw_finder(int cx, int cy) {
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
            set_func(cx + dx, cy + dy, d != 2 && d != 4);
        }
}

static void draw_alignment(int cx, int cy) {
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
            set_func(cx + dx, cy + dy, d != 1);
        }
}

/* биты формата (уровень M = 00) + маска */
static void draw_format(int mask) {
    unsigned data = (0u << 3) | (unsigned)mask;
    unsigned rem = data;
    for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    unsigned bits = ((data << 10) | rem) ^ 0x5412;

    for (int i = 0; i <= 5; i++) set_func(8, i, bit(bits, i));
    set_func(8, 7, bit(bits, 6));
    set_func(8, 8, bit(bits, 7));
    set_func(7, 8, bit(bits, 8));
    for (int i = 9; i < 15; i++) set_func(14 - i, 8, bit(bits, i));

    for (int i = 0; i < 8; i++) set_func(N - 1 - i, 8, bit(bits, i));
    for (int i = 8; i < 15; i++) set_func(8, N - 15 + i, bit(bits, i));
    set_func(8, N - 8, 1);   /* тёмный модуль */
}

static void draw_function_patterns(int version) {
    memset(mod, 0, sizeof(mod));
    memset(isf, 0, sizeof(isf));
    for (int i = 0; i < N; i++) {
        set_func(6, i, i % 2 == 0);
        set_func(i, 6, i % 2 == 0);
    }
    draw_finder(3, 3);
    draw_finder(N - 4, 3);
    draw_finder(3, N - 4);
    if (version >= 2) draw_alignment(N - 7, N - 7);
    draw_format(0);   /* резервируем область; значения перепишем после выбора маски */
}

static void place_codewords(const uint8_t *cw, int total_bits) {
    int i = 0;
    for (int right = N - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < N; vert++) {
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                int upward = ((right + 1) & 2) == 0;
                int y = upward ? N - 1 - vert : vert;
                if (!isf[y][x] && i < total_bits) {
                    mod[y][x] = (uint8_t)((cw[i >> 3] >> (7 - (i & 7))) & 1);
                    i++;
                }
            }
        }
    }
}

static void apply_mask(int m) {
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            if (isf[y][x]) continue;
            int inv;
            switch (m) {
            case 0: inv = (x + y) % 2 == 0; break;
            case 1: inv = y % 2 == 0; break;
            case 2: inv = x % 3 == 0; break;
            case 3: inv = (x + y) % 3 == 0; break;
            case 4: inv = (x / 3 + y / 2) % 2 == 0; break;
            case 5: inv = (x * y) % 2 + (x * y) % 3 == 0; break;
            case 6: inv = ((x * y) % 2 + (x * y) % 3) % 2 == 0; break;
            default: inv = ((x + y) % 2 + (x * y) % 3) % 2 == 0; break;
            }
            mod[y][x] ^= (uint8_t)inv;
        }
}

/* ---------- штрафы для выбора маски ---------- */

static int line_penalty(const uint8_t *line) {
    int score = 0;
    int run = 1;
    for (int i = 1; i < N; i++) {
        if (line[i] == line[i - 1]) {
            run++;
            if (run == 5) score += 3;
            else if (run > 5) score += 1;
        } else {
            run = 1;
        }
    }
    /* шаблон, похожий на искатель: 1:1:3:1:1 с 4 светлыми с одной из сторон */
    static const uint8_t P1[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
    static const uint8_t P2[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
    for (int i = 0; i + 11 <= N; i++) {
        if (memcmp(line + i, P1, 11) == 0) score += 40;
        if (memcmp(line + i, P2, 11) == 0) score += 40;
    }
    return score;
}

static int penalty(void) {
    int score = 0;
    uint8_t col[QR_MAX_SIZE];
    for (int y = 0; y < N; y++) score += line_penalty(mod[y]);
    for (int x = 0; x < N; x++) {
        for (int y = 0; y < N; y++) col[y] = mod[y][x];
        score += line_penalty(col);
    }
    for (int y = 0; y + 1 < N; y++)
        for (int x = 0; x + 1 < N; x++) {
            uint8_t c = mod[y][x];
            if (c == mod[y][x + 1] && c == mod[y + 1][x] && c == mod[y + 1][x + 1]) score += 3;
        }
    int dark = 0;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) dark += mod[y][x];
    int total = N * N;
    int k = (abs(dark * 20 - total * 10) + total - 1) / total - 1;
    if (k > 0) score += k * 10;
    return score;
}

/* ---------- основной вход ---------- */

int qr_encode(const char *text, uint8_t out[QR_MAX_SIZE * QR_MAX_SIZE]) {
    gf_init();
    int len = (int)strlen(text);

    int version = 0;
    for (int v = 1; v <= MAXV; v++) {
        int data_cw = VT[v].total_cw - VT[v].ec_per_block * VT[v].blocks;
        if (len <= (data_cw * 8 - 12) / 8) { version = v; break; }
    }
    if (!version) return 0;
    N = 17 + 4 * version;

    const vinfo_t *vi = &VT[version];
    int data_cw = vi->total_cw - vi->ec_per_block * vi->blocks;

    /* 1) битовый поток данных */
    uint8_t data[128];
    memset(data, 0, sizeof(data));
    int nbits = 0;
#define PUT(val, cnt) do { for (int _i = (cnt) - 1; _i >= 0; _i--) { \
        if (((val) >> _i) & 1) { data[nbits >> 3] |= (uint8_t)(0x80 >> (nbits & 7)); } \
        nbits++; } } while (0)
    PUT(0x4, 4);               /* режим: байты */
    PUT(len, 8);               /* длина (версии 1..9) */
    for (int i = 0; i < len; i++) PUT((unsigned char)text[i], 8);
    int cap = data_cw * 8;
    int term = cap - nbits < 4 ? cap - nbits : 4;
    nbits += term;
    nbits = (nbits + 7) & ~7;
    for (int pad = 0xEC; (nbits >> 3) < data_cw; pad ^= 0xEC ^ 0x11) {
        data[nbits >> 3] = (uint8_t)pad;
        nbits += 8;
    }

    /* 2) блоки и коррекция ошибок, чередование */
    int nb = vi->blocks, e = vi->ec_per_block;
    int dpb = data_cw / nb;   /* для M, версии 1..6 все блоки одной длины */
    uint8_t div[32], ecc[4][32];
    rs_divisor(e, div);
    for (int b = 0; b < nb; b++) rs_remainder(data + b * dpb, dpb, div, e, ecc[b]);

    uint8_t cw[200];
    int k = 0;
    for (int i = 0; i < dpb; i++) for (int b = 0; b < nb; b++) cw[k++] = data[b * dpb + i];
    for (int i = 0; i < e; i++) for (int b = 0; b < nb; b++) cw[k++] = ecc[b][i];

    /* 3) матрица, лучшая маска */
    draw_function_patterns(version);
    place_codewords(cw, vi->total_cw * 8);

    static uint8_t base[QR_MAX_SIZE][QR_MAX_SIZE];
    memcpy(base, mod, sizeof(mod));
    int best = 0, best_score = 1 << 30;
    for (int m = 0; m < 8; m++) {
        memcpy(mod, base, sizeof(mod));
        apply_mask(m);
        draw_format(m);
        int s = penalty();
        if (s < best_score) { best_score = s; best = m; }
    }
    memcpy(mod, base, sizeof(mod));
    apply_mask(best);
    draw_format(best);

    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) out[y * N + x] = mod[y][x];
    return N;
}
