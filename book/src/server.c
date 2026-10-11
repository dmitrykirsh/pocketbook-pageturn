/*
 * PageTurn server v3 — UDP-сервер на PocketBook.
 * Протокол (UDP, порт 57321):
 *   "PTDISCOVER"          → ответ "PTHERE:<port>:<имя>" (автопоиск, PIN не нужен)
 *   "<PIN>:next|prev"     → нажатие физической кнопки активной читалке (InkView SDK)
 *   "<PIN>:ping"          → «телефон на связи»: держим Wi-Fi и не даём книге уснуть
 * PIN и имя читаются из конфига /mnt/ext1/system/pageturn/config (строки pin=…, name=…);
 * изменения подхватываются на лету. Если PIN нет — генерируется случайный 4-значный.
 * После 5 неверных PIN с одного адреса за минуту адрес блокируется на минуту.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define EVT_KEYPRESS   25
#define EVT_KEYRELEASE 26
#define IV_KEY_PREV 0x18
#define IV_KEY_NEXT 0x19
#define LOG_MAX (200 * 1024)
#define TICK_SEC 5
#define SESSION_SEC 240   /* телефон «на связи», если команды были за последние 4 мин */
#define CFG_FILE "/mnt/ext1/system/pageturn/config"
#define PID_FILE "/var/tmp/pageturn.pid"
#define MAX_FAIL 5
#define FAIL_WINDOW 60
#define BLOCK_SEC 60
#define MAX_TRACK 16

/* Начало struct taskinfo_s из inkview.h (нужны только эти поля). */
typedef struct {
    int task;
    int nsubtasks;
    unsigned int flags;
    int fbshmkey;
    int fbshmsize;
    int mainpid;
    char *appname;
} ti_head;
extern ti_head *GetTaskInfo(int task);
extern void GetActiveTask(int *task, int *subtask);
extern int FindTaskByAppName(const char *name);
extern int SendEventTo(int task, int type, int par1, int par2);
extern int WiFiPower(int on);
extern int NetConnect(const char *essid);
extern int NetMgrPing(void);
extern void BanSleep(int sec);
extern void InitInkview(int reg_flags);

static FILE *logf;
static char logpath[256];
static volatile sig_atomic_t g_sig = 0;

static char g_pin[16] = "";
static char g_name[48] = "PocketBook";
static time_t cfg_mtime = 0;

static void logmsg(const char *fmt, ...) {
    va_list ap;
    time_t t = time(NULL);
    struct tm *tmv = localtime(&t);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tmv);
    fprintf(logf, "[%s] ", ts);
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fprintf(logf, "\n");
    fflush(logf);

    struct stat st;
    if (stat(logpath, &st) == 0 && st.st_size > LOG_MAX) {
        fclose(logf);
        logf = fopen(logpath, "w");
        if (!logf) logf = stderr;
        fprintf(logf, "[%s] (log rotated)\n", ts);
        fflush(logf);
    }
}

static void on_sig(int s) { g_sig = s; }

/* ---------- конфиг ---------- */

static int pin_valid(const char *p) {
    size_t n = strlen(p);
    if (n < 4 || n > 8) return 0;
    for (size_t i = 0; i < n; i++) if (p[i] < '0' || p[i] > '9') return 0;
    return 1;
}

static void write_config(void) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s.tmp", CFG_FILE);
    FILE *f = fopen(tmp, "w");
    if (!f) { logmsg("config: не могу записать %s: %s", tmp, strerror(errno)); return; }
    fprintf(f, "pin=%s\nname=%s\n", g_pin, g_name);
    fclose(f);
    rename(tmp, CFG_FILE);
}

static void load_config(void) {
    struct stat st;
    if (stat(CFG_FILE, &st) != 0) return;
    cfg_mtime = st.st_mtime;
    FILE *f = fopen(CFG_FILE, "r");
    if (!f) return;
    char line[128];
    char pin[16] = "";
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' ')) line[--n] = 0;
        if (strncmp(line, "pin=", 4) == 0) snprintf(pin, sizeof(pin), "%s", line + 4);
        else if (strncmp(line, "name=", 5) == 0 && line[5]) snprintf(g_name, sizeof(g_name), "%s", line + 5);
    }
    fclose(f);
    if (pin_valid(pin)) {
        if (strcmp(pin, g_pin) != 0) logmsg("config: PIN обновлён");
        snprintf(g_pin, sizeof(g_pin), "%s", pin);
    }
}

static void ensure_pin(void) {
    load_config();
    if (pin_valid(g_pin)) return;
    srand((unsigned)(time(NULL) ^ (getpid() << 8)));
    snprintf(g_pin, sizeof(g_pin), "%04d", rand() % 10000);
    mkdir("/mnt/ext1/system/pageturn", 0777);
    write_config();
    logmsg("config: PIN не был задан — сгенерирован новый");
}

/* ---------- защита от подбора PIN ---------- */

typedef struct { uint32_t ip; int fails; time_t first; time_t blocked_until; } track_t;
static track_t track[MAX_TRACK];

static track_t *track_get(uint32_t ip, time_t now) {
    track_t *free_slot = NULL, *oldest = &track[0];
    for (int i = 0; i < MAX_TRACK; i++) {
        if (track[i].ip == ip && ip) return &track[i];
        if (!track[i].ip && !free_slot) free_slot = &track[i];
        if (track[i].first < oldest->first) oldest = &track[i];
    }
    track_t *t = free_slot ? free_slot : oldest;
    memset(t, 0, sizeof(*t));
    t->ip = ip;
    t->first = now;
    return t;
}

static int is_blocked(uint32_t ip, time_t now) {
    track_t *t = track_get(ip, now);
    return t->blocked_until > now;
}

static void note_failure(struct in_addr a, time_t now) {
    track_t *t = track_get(a.s_addr, now);
    if (now - t->first > FAIL_WINDOW) { t->first = now; t->fails = 0; }
    t->fails++;
    if (t->fails >= MAX_FAIL) {
        t->blocked_until = now + BLOCK_SEC;
        t->fails = 0;
        logmsg("защита: %s заблокирован на %d с (неверный PIN)", inet_ntoa(a), BLOCK_SEC);
    }
}

/* ---------- сеть ---------- */

static uint32_t get_ip(int s, const char *ifname) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFADDR, &ifr) < 0) return 0;
    return ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
}

static uint32_t net_ip(int s) {
    uint32_t ip = get_ip(s, "eth0");
    if (!ip) ip = get_ip(s, "wlan0");
    return ip;
}

static time_t last_phone = 0;
static int phone_online = 0;
static uint32_t last_ip = 0xffffffffu;
static time_t last_reconnect = 0, last_netping = 0, last_ban = 0, last_hb = 0;

static void housekeeping(int sock, time_t now) {
    struct stat st;
    if (stat(CFG_FILE, &st) == 0 && st.st_mtime != cfg_mtime) load_config();

    uint32_t ip = net_ip(sock);
    if (ip != last_ip) {
        struct in_addr a; a.s_addr = ip;
        logmsg("сеть: IP = %s", ip ? inet_ntoa(a) : "(нет)");
        last_ip = ip;
    }
    if (ip == 0) {
        if (now - last_reconnect >= 10) {
            last_reconnect = now;
            int a = WiFiPower(1);
            int b = NetConnect(NULL);
            logmsg("сеть: нет IP → WiFiPower(1)=%d, NetConnect(NULL)=%d", a, b);
        }
    } else if (now - last_netping >= 20) {
        last_netping = now;
        NetMgrPing();
    }

    int online = (last_phone != 0 && now - last_phone < SESSION_SEC);
    if (online != phone_online) {
        phone_online = online;
        if (online) logmsg("телефон на связи — запрещаю сон");
        else logmsg("телефон не выходил на связь %d с — сон разрешён", SESSION_SEC);
    }
    if (online && now - last_ban >= 10) {
        last_ban = now;
        BanSleep(30);
    }

    if (now - last_hb >= 300) {
        last_hb = now;
        struct in_addr a; a.s_addr = ip;
        logmsg("пульс: ip=%s телефон=%s", ip ? inet_ntoa(a) : "(нет)", online ? "на связи" : "нет");
    }
}

int main(int argc, char **argv) {
    /* argv: [1]=logpath [2]=appname [3]=port */
    snprintf(logpath, sizeof(logpath), "%s", argc > 1 ? argv[1] : "/mnt/ext1/system/pageturn/pageturn.log");
    const char *appname = argc > 2 ? argv[2] : "eink-reader.app";
    int port = argc > 3 ? atoi(argv[3]) : 57321;

    logf = fopen(logpath, "a");
    if (!logf) { logf = stderr; }
    logmsg("PageTurn server v3 start, pid=%d port=%d", (int)getpid(), port);

    FILE *pf = fopen(PID_FILE, "w");
    if (pf) { fprintf(pf, "%d\n", (int)getpid()); fclose(pf); }

    ensure_pin();
    InitInkview(0);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sig;
    sigemptyset(&sa.sa_mask);
    int sigs[] = {SIGTERM, SIGHUP, SIGUSR1, SIGUSR2, SIGINT, SIGQUIT, SIGPIPE};
    for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) sigaction(sigs[i], &sa, NULL);

    WiFiPower(1);
    NetConnect(NULL);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { logmsg("socket() failed: %s", strerror(errno)); return 1; }
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        logmsg("bind() failed: %s", strerror(errno));
        return 1;
    }
    logmsg("listening on UDP :%d", port);

    char buf[256];
    struct sockaddr_in src;
    socklen_t srclen;
    time_t last_tick = time(NULL);
    time_t last_house = 0;

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        struct timeval tv;
        tv.tv_sec = TICK_SEC;
        tv.tv_usec = 0;
        int rs = select(sock + 1, &rfds, NULL, NULL, &tv);

        time_t now = time(NULL);
        if (g_sig) { logmsg("получен сигнал %d (игнорирую)", (int)g_sig); g_sig = 0; }
        if (now - last_tick > 30) {
            logmsg("⚠ пауза %ld с между итерациями — книга спала/была заморожена; проверяю сеть",
                   (long)(now - last_tick));
            last_house = 0;
            last_reconnect = 0;
            last_ip = 0xffffffffu;
        }
        last_tick = now;

        if (now - last_house >= TICK_SEC) { last_house = now; housekeeping(sock, now); }

        if (rs < 0) { if (errno != EINTR) logmsg("select() error: %s", strerror(errno)); continue; }
        if (rs == 0) continue;

        srclen = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf)-1, 0, (struct sockaddr*)&src, &srclen);
        if (n <= 0) continue;
        buf[n] = 0;
        for (int i = 0; i < n; i++) if (buf[i]=='\n' || buf[i]=='\r') { buf[i]=0; break; }

        /* автопоиск: без PIN */
        if (strcmp(buf, "PTDISCOVER") == 0) {
            char reply[96];
            int rn = snprintf(reply, sizeof(reply), "PTHERE:%d:%s", port, g_name);
            sendto(sock, reply, rn, 0, (struct sockaddr*)&src, srclen);
            logmsg("поиск: ответил %s", inet_ntoa(src.sin_addr));
            continue;
        }

        if (is_blocked(src.sin_addr.s_addr, now)) continue;

        size_t pl = strlen(g_pin);
        if (pl == 0 || strncmp(buf, g_pin, pl) != 0 || buf[pl] != ':') {
            logmsg("REJECTED (неверный PIN) от %s:%d", inet_ntoa(src.sin_addr), ntohs(src.sin_port));
            note_failure(src.sin_addr, now);
            continue;
        }
        const char *cmd = buf + pl + 1;
        last_phone = now;

        if (strcmp(cmd, "ping") == 0) {
            /* подтверждаем телефону, что адрес и PIN верные */
            sendto(sock, "PTOK", 4, 0, (struct sockaddr*)&src, srclen);
            continue;
        }

        int key = 0;
        if (strcmp(cmd, "next") == 0) key = IV_KEY_NEXT;
        else if (strcmp(cmd, "prev") == 0) key = IV_KEY_PREV;
        if (!key) {
            logmsg("recv unknown cmd '%s' from %s:%d", cmd, inet_ntoa(src.sin_addr), ntohs(src.sin_port));
            continue;
        }

        /* Целимся в активную задачу (то, что сейчас на экране). */
        int atask = -1, asub = -1;
        GetActiveTask(&atask, &asub);
        ti_head *ti = atask > 0 ? GetTaskInfo(atask) : NULL;
        const char *aname = (ti && ti->appname) ? ti->appname : "?";
        int bytask = FindTaskByAppName(appname);

        int task = atask > 0 ? atask : bytask;
        if (task <= 0) {
            logmsg("cmd '%s': нет целевой задачи (active=%d byname=%d)", cmd, atask, bytask);
            continue;
        }
        int r1 = SendEventTo(task, EVT_KEYPRESS, key, 0);
        usleep(80000);
        int r2 = SendEventTo(task, EVT_KEYRELEASE, key, 0);
        logmsg("cmd '%s' from %s:%d -> task=%d (%s) press=%d release=%d",
               cmd, inet_ntoa(src.sin_addr), ntohs(src.sin_port), task, aname, r1, r2);
    }
    return 0;
}
