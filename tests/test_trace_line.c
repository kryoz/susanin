/* Тесты строки признаков автообучения (этап 0: сбор датасета).
 * classifier.c включается в TU, чтобы дотянуться до static trace_domain. */
#define _GNU_SOURCE
#include "classifier.c"
#include "classifier.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok  %s\n", msg); \
    else { printf("FAIL %s\n", msg); failures++; } } while (0)

static void fill_flow(ct_flow *f)
{
    memset(f, 0, sizeof(*f));
    f->l4proto = 6;
    snprintf(f->proto, sizeof(f->proto), "%s", "tcp");
    snprintf(f->src, sizeof(f->src), "%s", "192.168.0.50");
    snprintf(f->dst, sizeof(f->dst), "%s", "203.0.113.7");
    snprintf(f->tcp_state, sizeof(f->tcp_state), "%s", "SYN_SENT");
    f->sport = 51234;
    f->dport = 443;
    f->op = 3;
    f->ob = 180;
    f->rp = 0;
    f->rb = 0;
    f->has_reply = 0;
    f->fastnat = 1;
}

static int has_substr(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

int main(void)
{
    ct_flow f;
    char buf[320];

    fill_flow(&f);

    printf("=== базовая строка признаков ===\n");
    clf_trace_line(buf, sizeof(buf), &f, "FAST", "TCP-SYN", "");
    printf("    %s\n", buf);
    CHECK(has_substr(buf, "TRC sig=FAST/TCP-SYN"), "стадия и сигнал");
    CHECK(has_substr(buf, "pr=tcp") && has_substr(buf, "st=SYN_SENT"),
          "протокол и состояние TCP");
    CHECK(has_substr(buf, "dp=443") && has_substr(buf, "sp=51234"), "порты");
    CHECK(has_substr(buf, "op=3") && has_substr(buf, "ob=180") &&
          has_substr(buf, "rp=0") && has_substr(buf, "rb=0"), "счётчики потока");
    CHECK(has_substr(buf, "hr=0") && has_substr(buf, "fn=1"),
          "has_reply/fastnat (fastnat меняет пороги judge)");
    CHECK(has_substr(buf, "dom=-"), "домен неизвестен — прочерк, а не пустое поле");
    clf_trace_line(buf, sizeof(buf), &f, "FAST", "TCP-SYN", NULL);
    CHECK(has_substr(buf, "dom=-"), "NULL вместо домена — тоже прочерк");

    printf("=== домен из DNS-ответа ===\n");
    clf_trace_line(buf, sizeof(buf), &f, "SOFT", "TCP-STALL", "ya.ru");
    CHECK(has_substr(buf, "dom=ya.ru"), "обычный домен пишется как есть");

    printf("=== домен — данные из сети: инъекция в лог ===\n");
    clf_trace_line(buf, sizeof(buf), &f, "FAST", "DPI-RST",
                   "evil.ru\n10-30 00:00:00 INFO: AUTO-SUSANIN: CONFIRMED 1.2.3.4:443");
    CHECK(!strchr(buf, '\n') && !strchr(buf, '\r'),
          "перевод строки из домена не разрывает запись лога");
    CHECK(!strchr(buf, ':'),
          "двоеточие вычищено — подставить «INFO: … CONFIRMED» из домена нельзя");
    CHECK(strchr(buf, '?') != NULL, "запрещённые символы заменяются");

    printf("=== усечение без выхода за границу ===\n");
    {
        char long_dom[256];
        char small[64];
        char guard[257];
        const char *p;
        unsigned i;
        memset(long_dom, 'a', sizeof(long_dom));
        long_dom[sizeof(long_dom) - 1] = '\0';
        clf_trace_line(buf, sizeof(buf), &f, "FAST", "TCP-SYN", long_dom);
        CHECK(strlen(buf) < sizeof(buf), "длинный домен: строка влезла в буфер");
        for (p = buf, i = 0; *p; p++)
            if (*p == 'a')
                i++;
        CHECK(i < 128, "домен усечён до 127 символов (dom[128]), не целиком");

        memset(small, 'Z', sizeof(small));
        clf_trace_line(small, 1, &f, "FAST", "TCP-SYN", "ya.ru");
        CHECK(small[0] == '\0', "буфер в 1 байт: только терминатор");

        memset(guard, 'x', sizeof(guard));
        guard[256] = '\0';
        clf_trace_line(guard, 40, &f, "FAST", "TCP-SYN", "example.com");
        CHECK(strlen(guard) < 40 && guard[40] == 'x',
              "за terminator ничего не записано");
    }

    printf("=== UDP-поток (QUIC) ===\n");
    f.l4proto = 17;
    snprintf(f.proto, sizeof(f.proto), "%s", "udp");
    snprintf(f.tcp_state, sizeof(f.tcp_state), "%s", "");
    clf_trace_line(buf, sizeof(buf), &f, "FAST", "QUIC", "quic.example.net");
    printf("    %s\n", buf);
    CHECK(has_substr(buf, "pr=udp") && has_substr(buf, "sig=FAST/QUIC"),
          "udp-признаки");
    CHECK(has_substr(buf, "st= dp="), "пустое tcp_state не сдвигает поля");

    printf("\n%s\n", failures ? "ЕСТЬ ОШИБКИ" : "все проверки пройдены");
    return failures ? 1 : 0;
}
