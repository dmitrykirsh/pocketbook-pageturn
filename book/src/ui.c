/*
 * PageTurn UI — экран приложения на книге: статус сервера, адрес, порт, PIN,
 * смена PIN (цифровая клавиатура) и случайный PIN. Язык — из global.cfg.
 * Сервер (pageturn) работает отдельным процессом и продолжает работать после выхода.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "inkview.h"
#include "i18n_book.h"
#include "qr.h"

#define CFG_DIR   "/mnt/ext1/system/pageturn"
#define CFG_FILE  CFG_DIR "/config"
#define PID_FILE  "/var/tmp/pageturn.pid"
#define GLOBAL_CFG "/mnt/ext1/system/config/global.cfg"
#define PORT 57321

typedef struct { int x, y, w, h; } rect_t;

static int W, H;
static ifont *f_title, *f_body, *f_small, *f_pin, *f_btn;
static const book_lang_t *L;
static char g_pin[16] = "";
static char g_name[48] = "PocketBook";
static char kb_buf[16];
static const char *g_msg = NULL;
static rect_t b_change, b_random, b_refresh, b_close, b_qr, b_back;
static int first_draw = 1;
static int g_page = 0;   /* 0 — главный экран, 1 — QR */

#define S(i) (L->s[i])

/* ---------- отладка: журнал в stderr (лаунчер пишет его в ui.log) ---------- */
#include <stdarg.h>
#include <time.h>
static long t0_ms = 0;
static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}
static int dbg_lines = 0;
static void dbg(const char *fmt, ...) {
    if (dbg_lines++ > 600) return;
    va_list ap;
    fprintf(stderr, "[%6ld] ", now_ms() - t0_ms);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}
static long init_ms = 0;

/* ---------- язык ---------- */

static void detect_language(void) {
    char code[8] = "en";
    FILE *f = fopen(GLOBAL_CFG, "r");
    if (f) {
        char line[160];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "language=", 9) == 0) {
                size_t n = 0;
                const char *p = line + 9;
                while (p[n] && p[n] != '\n' && p[n] != '\r' && n < sizeof(code) - 1) { code[n] = p[n]; n++; }
                code[n] = 0;
                break;
            }
        }
        fclose(f);
    }
    L = &BOOK_LANGS[0];
    for (int i = 0; i < BOOK_LANG_COUNT; i++)
        if (strcmp(BOOK_LANGS[i].code, code) == 0) { L = &BOOK_LANGS[i]; break; }
}

/* ---------- конфиг ---------- */

static int pin_valid(const char *p) {
    size_t n = strlen(p);
    if (n < 4 || n > 8) return 0;
    for (size_t i = 0; i < n; i++) if (p[i] < '0' || p[i] > '9') return 0;
    return 1;
}

static void save_config(void) {
    char tmp[128];
    mkdir(CFG_DIR, 0777);
    snprintf(tmp, sizeof(tmp), "%s.tmp", CFG_FILE);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "pin=%s\nname=%s\n", g_pin, g_name);
    fclose(f);
    rename(tmp, CFG_FILE);
}

static void load_config(void) {
    FILE *f = fopen(CFG_FILE, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line);
            while (n && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' ')) line[--n] = 0;
            if (strncmp(line, "pin=", 4) == 0 && pin_valid(line + 4)) snprintf(g_pin, sizeof(g_pin), "%s", line + 4);
            else if (strncmp(line, "name=", 5) == 0 && line[5]) snprintf(g_name, sizeof(g_name), "%s", line + 5);
        }
        fclose(f);
    }
    if (!pin_valid(g_pin)) {
        srand((unsigned)(time(NULL) ^ (getpid() << 8)));
        snprintf(g_pin, sizeof(g_pin), "%04d", rand() % 10000);
        save_config();
    }
}

static void random_pin(void) {
    srand((unsigned)(time(NULL) ^ (getpid() << 8) ^ (unsigned)clock()));
    snprintf(g_pin, sizeof(g_pin), "%04d", rand() % 10000);
    save_config();
    g_msg = S(B_SAVED);
}

/* ---------- состояние ---------- */

static int server_running(void) {
    FILE *f = fopen(PID_FILE, "r");
    if (!f) return 0;
    int pid = 0;
    if (fscanf(f, "%d", &pid) != 1) pid = 0;
    fclose(f);
    return pid > 1 && kill(pid, 0) == 0;
}

static int get_ip_str(char *out, size_t n) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return 0;
    static const char *names[] = { "eth0", "wlan0" };
    for (int i = 0; i < 2; i++) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, names[i], IFNAMSIZ - 1);
        if (ioctl(s, SIOCGIFADDR, &ifr) == 0) {
            struct in_addr a = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
            if (a.s_addr) { snprintf(out, n, "%s", inet_ntoa(a)); close(s); return 1; }
        }
    }
    close(s);
    return 0;
}

/* ---------- рисование ---------- */

static void load_fonts(void) {
    int u = W / 24;                       /* базовая единица ~ высота строки */
    f_title = OpenFont("Roboto-Bold", u * 2, 1);
    f_body  = OpenFont("Roboto-Regular", u, 1);
    f_small = OpenFont("Roboto-Regular", (u * 3) / 4, 1);
    f_pin   = OpenFont("Roboto-Bold", u * 4, 1);
    f_btn   = OpenFont("Roboto-Bold", u, 1);
    const ifont *def = GetFont();
    if (!f_title) f_title = (ifont *)def;
    if (!f_body)  f_body  = (ifont *)def;
    if (!f_small) f_small = (ifont *)def;
    if (!f_pin)   f_pin   = (ifont *)def;
    if (!f_btn)   f_btn   = (ifont *)def;
}

static void draw_button(const rect_t *r, const char *text) {
    DrawRect(r->x, r->y, r->w, r->h, BLACK);
    DrawRect(r->x + 1, r->y + 1, r->w - 2, r->h - 2, BLACK);
    SetFont(f_btn, BLACK);
    DrawTextRect(r->x, r->y, r->w, r->h, text, ALIGN_CENTER | VALIGN_MIDDLE);
}

static void draw_main(void) {
    W = ScreenWidth();
    H = ScreenHeight();
    dbg("draw_main W=%d H=%d orient=%d", W, H, GetOrientation());
    int m = W / 20;
    int u = W / 24;
    int line = (u * 3) / 2;
    int th = (u * 5) / 2;
    int y = m;
    char buf[640]; /* UTF-8: кириллица ~2 байта/символ, подсказка из двух строк длинная */

    ClearScreen();

    SetFont(f_title, BLACK);
    DrawTextRect(m, y, W - 2 * m, th, S(B_TITLE), ALIGN_CENTER | VALIGN_MIDDLE);
    y += th;
    FillArea(m, y, W - 2 * m, 3, BLACK);
    y += m;

    /* статус */
    SetFont(f_body, BLACK);
    DrawTextRect(m, y, W - 2 * m, line,
                 server_running() ? S(B_SERVER_OK) : S(B_SERVER_DOWN), ALIGN_LEFT | VALIGN_MIDDLE);
    y += line;
    char ip[32];
    if (get_ip_str(ip, sizeof(ip))) snprintf(buf, sizeof(buf), S(B_WIFI_FMT), ip);
    else snprintf(buf, sizeof(buf), "%s", S(B_WIFI_NONE));
    DrawTextRect(m, y, W - 2 * m, line, buf, ALIGN_LEFT | VALIGN_MIDDLE);
    y += line;
    snprintf(buf, sizeof(buf), S(B_PORT_FMT), PORT);
    DrawTextRect(m, y, W - 2 * m, line, buf, ALIGN_LEFT | VALIGN_MIDDLE);
    y += line + m / 2;

    /* PIN */
    SetFont(f_small, DGRAY);
    DrawTextRect(m, y, W - 2 * m, u, S(B_PIN_LABEL), ALIGN_CENTER | VALIGN_MIDDLE);
    y += u;
    int ph = u * 4;
    DrawRect(m, y, W - 2 * m, ph, BLACK);
    DrawRect(m + 1, y + 1, W - 2 * m - 2, ph - 2, BLACK);
    char spaced[40];
    size_t k = 0;
    for (size_t i = 0; g_pin[i] && k < sizeof(spaced) - 3; i++) {
        spaced[k++] = g_pin[i];
        if (g_pin[i + 1]) spaced[k++] = ' ';
    }
    spaced[k] = 0;
    SetFont(f_pin, BLACK);
    DrawTextRect(m, y, W - 2 * m, ph, spaced, ALIGN_CENTER | VALIGN_MIDDLE);
    y += ph;

    /* сообщение */
    SetFont(f_small, BLACK);
    DrawTextRect(m, y, W - 2 * m, (u * 3) / 2, g_msg ? g_msg : "", ALIGN_CENTER | VALIGN_MIDDLE);
    y += (u * 3) / 2;

    /* кнопки 2x2 */
    int bw = (W - 3 * m) / 2;
    int bh = (u * 5) / 2;
    int gap = m / 2;
    b_change  = (rect_t){ m,           y,                    bw,         bh };
    b_random  = (rect_t){ 2 * m + bw,  y,                    bw,         bh };
    b_qr      = (rect_t){ m,           y + bh + gap,         bw,         bh };
    b_refresh = (rect_t){ 2 * m + bw,  y + bh + gap,         bw,         bh };
    b_close   = (rect_t){ m,           y + 2 * (bh + gap),   W - 2 * m,  bh };
    dbg("rects: change y=%d..%d qr y=%d..%d close y=%d..%d (x %d..%d / %d..%d)", b_change.y, b_change.y + b_change.h, b_qr.y, b_qr.y + b_qr.h, b_close.y, b_close.y + b_close.h, b_change.x, b_change.x + b_change.w, b_random.x, b_random.x + b_random.w);
    draw_button(&b_change, S(B_BTN_CHANGE));
    draw_button(&b_random, S(B_BTN_RANDOM));
    draw_button(&b_qr, S(B_BTN_QR));
    draw_button(&b_refresh, S(B_BTN_REFRESH));
    draw_button(&b_close, S(B_BTN_CLOSE));
    y += 3 * bh + 2 * gap + m;

    /* подсказка */
    int hh = H - y - m / 2;
    if (hh > u) {
        SetFont(f_small, DGRAY);
        snprintf(buf, sizeof(buf), "%s\n%s", S(B_HINT1), S(B_HINT2));
        DrawTextRect(m, y, W - 2 * m, hh, buf, ALIGN_LEFT | VALIGN_TOP);
    }

    FullUpdate();   /* SoftUpdate давал сдвиг картинки с переносом по вертикали */
}

/* страница с QR-кодом: pageturn://c?h=<адрес>&p=<порт>&k=<PIN> */
static void draw_qr(void) {
    W = ScreenWidth();
    H = ScreenHeight();
    int m = W / 20;
    int u = W / 24;
    int y = m;
    char ip[32], payload[128], buf[160];
    static uint8_t modules[QR_MAX_SIZE * QR_MAX_SIZE];

    ClearScreen();

    SetFont(f_title, BLACK);
    DrawTextRect(m, y, W - 2 * m, (u * 5) / 2, "PageTurn", ALIGN_CENTER | VALIGN_MIDDLE);
    y += (u * 5) / 2;
    FillArea(m, y, W - 2 * m, 3, BLACK);
    y += m / 2;

    SetFont(f_small, DGRAY);
    DrawTextRect(m, y, W - 2 * m, u * 3, S(B_QR_HINT), ALIGN_CENTER | VALIGN_MIDDLE);
    y += u * 3;

    int bh = (u * 5) / 2;
    b_back = (rect_t){ m, H - m - bh, W - 2 * m, bh };

    if (!get_ip_str(ip, sizeof(ip))) {
        SetFont(f_body, BLACK);
        DrawTextRect(m, y, W - 2 * m, u * 4, S(B_WIFI_NONE), ALIGN_CENTER | VALIGN_MIDDLE);
    } else {
        snprintf(payload, sizeof(payload), "pageturn://c?h=%s&p=%d&k=%s", ip, PORT, g_pin);
        int size = qr_encode(payload, modules);
        if (size > 0) {
            int avail_w = W - 2 * m;
            int avail_h = b_back.y - y - m - u * 3;      /* место под строку с адресом */
            int cells = size + 8;                        /* + тихая зона 4 модуля с каждой стороны */
            int ms = avail_w / cells;
            if (avail_h / cells < ms) ms = avail_h / cells;
            if (ms < 2) ms = 2;
            int side = cells * ms;
            int x0 = (W - side) / 2;
            int y0 = y;
            FillArea(x0, y0, side, side, WHITE);
            for (int r = 0; r < size; r++)
                for (int c = 0; c < size; c++)
                    if (modules[r * size + c])
                        FillArea(x0 + (c + 4) * ms, y0 + (r + 4) * ms, ms, ms, BLACK);
            y = y0 + side;
        }
        SetFont(f_small, BLACK);
        snprintf(buf, sizeof(buf), "%s:%d   PIN %s", ip, PORT, g_pin);
        DrawTextRect(m, y, W - 2 * m, u * 3, buf, ALIGN_CENTER | VALIGN_MIDDLE);
    }

    draw_button(&b_back, S(B_BTN_BACK));
    FullUpdate();   /* SoftUpdate давал сдвиг картинки с переносом по вертикали */
}

static void draw_screen(void) {
    if (g_page == 1) draw_qr();
    else draw_main();
}

/* ---------- ввод ---------- */

static void kb_done(char *text) {
    if (text && text[0]) {
        if (pin_valid(text)) {
            snprintf(g_pin, sizeof(g_pin), "%s", text);
            save_config();
            g_msg = S(B_SAVED);
        } else {
            g_msg = S(B_ERR_PIN);
        }
    }
    draw_screen();
}

static int hit(const rect_t *r, int x, int y) {
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static void redraw_timer(void) {
    dbg("timer: перерисовка");
    draw_screen();
}

static void on_tap(int x, int y) {
    dbg("TAP x=%d y=%d page=%d since_init=%ldms", x, y, g_page, now_ms() - init_ms);
    if (now_ms() - init_ms < 400) { dbg("  -> проигнорировано (первые 400 мс после старта)"); return; }
    if (g_page == 1) {
        if (hit(&b_back, x, y)) { g_page = 0; draw_screen(); }
        return;
    }
    if (hit(&b_qr, x, y)) {
        g_page = 1;
        draw_screen();
    } else if (hit(&b_change, x, y)) {
        kb_buf[0] = 0;
        g_msg = NULL;
        OpenKeyboard(S(B_KB_TITLE), kb_buf, 8, KBD_NUMERIC, kb_done);
    } else if (hit(&b_random, x, y)) {
        random_pin();
        draw_screen();
    } else if (hit(&b_refresh, x, y)) {
        g_msg = NULL;
        draw_screen();
    } else if (hit(&b_close, x, y)) {
        CloseApp();
    }
}

static int main_handler(int type, int par1, int par2) {
    if (t0_ms == 0) t0_ms = now_ms();
    dbg("evt type=%d p1=%d p2=%d", type, par1, par2);
    switch (type) {
    case EVT_INIT:
        init_ms = now_ms();
        dbg("панель ДО: type=%d height=%d fb_offset=%d", GetPanelType(), PanelHeight(), PanelHeightFBOffset());
        SetPanelType(PANEL_DISABLED);   /* без панели нет сдвига буфера кадра на её высоту */
        dbg("панель ПОСЛЕ: type=%d height=%d fb_offset=%d", GetPanelType(), PanelHeight(), PanelHeightFBOffset());
        detect_language();
        W = ScreenWidth();
        H = ScreenHeight();
        load_fonts();
        load_config();
        draw_screen();
        break;
    case EVT_SHOW:
    case EVT_REPAINT:
    case EVT_ORIENTATION:
        draw_screen();
        break;
    case EVT_FOREGROUND:
    case EVT_ACTIVATE:
    case EVT_FOCUS:
        /* картинка, нарисованная до перехода приложения на передний план, показывается
         * со сдвигом — перерисовываем сразу и ещё раз чуть позже */
        detect_language();   /* язык системы мог смениться, пока процесс жил в фоне */
        draw_screen();
        SetHardTimer("pt_redraw", redraw_timer, 400);
        break;
    case EVT_KEYPRESS:
        if (par1 == IV_KEY_BACK && g_page == 1) { g_page = 0; draw_screen(); }
        else if (par1 == IV_KEY_BACK || par1 == IV_KEY_HOME) CloseApp();
        break;
    case EVT_POINTERUP:
        on_tap(par1, par2);
        break;
    default:
        break;
    }
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    InkViewMain(main_handler);
    return 0;
}
