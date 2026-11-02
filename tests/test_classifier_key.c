/* Тесты ключей port-aware в классификаторе (этап 5): pair_key/flow_port.
 * classifier.c включается в TU, чтобы дотянуться до static-функций. make test. */
#define _GNU_SOURCE
#include "classifier.c"
#include "classifier.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok  %s\n", msg); \
    else { printf("FAIL %s\n", msg); failures++; } } while (0)

int main(void)
{
    susanin_config cfg;
    ct_flow f;
    char buf[64];
    const char *key;

    config_set_defaults(&cfg);
    memset(&f, 0, sizeof(f));
    snprintf(f.dst, sizeof(f.dst), "%s", "203.0.113.9");
    f.dport = 443;

    printf("=== port_aware=0: ключ — адрес ===\n");
    key = pair_key(buf, sizeof(buf), &cfg, &f);
    CHECK(!strcmp(key, "203.0.113.9"), "пара не используется");
    CHECK(flow_port(&cfg, &f) == 0, "порт для ipset = 0");

    printf("=== port_aware=1: ключ — пара, порт передаётся ===\n");
    cfg.port_aware = 1;
    key = pair_key(buf, sizeof(buf), &cfg, &f);
    CHECK(!strcmp(key, "203.0.113.9:443"), "ключ ip:port");
    CHECK(flow_port(&cfg, &f) == 443, "порт для ipset = dport");
    f.dport = 80;
    key = pair_key(buf, sizeof(buf), &cfg, &f);
    CHECK(!strcmp(key, "203.0.113.9:80"), "другой порт — другой ключ");

    printf("=== ключ состояния совпадает с разбором ===\n");
    {
        char ip[64];
        int port = -1;
        f.dport = 8443;
        key = pair_key(buf, sizeof(buf), &cfg, &f);
        state_key_split(key, ip, sizeof(ip), &port);
        CHECK(!strcmp(ip, "203.0.113.9") && port == 8443,
              "state_key/pair_key/state_key_split согласованы");
    }

    printf("=== приватные сети = rule_priv() из tools/datapath.sh ===\n");
    CHECK(clf_is_private_dst("172.16.10.72") && clf_is_private_dst("192.168.0.1") &&
          clf_is_private_dst("10.0.0.5") && clf_is_private_dst("100.64.0.1") &&
          clf_is_private_dst("169.254.1.1") && clf_is_private_dst("127.0.0.1"),
          "все сети из rule_priv — приватные (vpn_always их не пинит)");
    CHECK(!clf_is_private_dst("8.8.8.8") && !clf_is_private_dst("172.15.0.1") &&
          !clf_is_private_dst("172.32.0.1") && !clf_is_private_dst("100.128.0.1"),
          "публичные и адреса сразу за границами диапазонов — не приватные");

    printf("=== quic_block=1: QUIC-кандидатов не заводим ===\n");
    {
        susanin_state st;
        classifier_ctx c;
        ct_flow q;
        config_set_defaults(&cfg);
        snprintf(cfg.lan_subnets, sizeof(cfg.lan_subnets), "%s", "192.168.0.0/24");
        state_init(&st);
        c.cfg = &cfg;
        c.st = &st;
        c.tunnel_down_at = 0;
        memset(&q, 0, sizeof(q));
        q.l4proto = 17;
        snprintf(q.proto, sizeof(q.proto), "%s", "udp");
        snprintf(q.src, sizeof(q.src), "%s", "192.168.0.50");
        snprintf(q.dst, sizeof(q.dst), "%s", "203.0.113.20");
        q.dport = 443;
        q.op = 10;                    /* выше порога FAST/QUIC */
        cfg.quic_block = 1;
        clr_fast(&c, &q, 1, 1000);
        CHECK(!state_has(st_test(&st, 1), "203.0.113.20", 1000),
              "UDP/443 не уходит в test: пакеты дропаются до наших меток");

    printf("=== rf_seen: отказ пишется один раз за окно ===\n");
    {
        time_t t0 = 1000000;
        CHECK(rf_seen("203.0.113.30", "never", t0) == 0, "первый отказ — записан");
        CHECK(rf_seen("203.0.113.30", "never", t0 + 1) == 1,
              "повтор той же причины в окне — молчит");
        CHECK(rf_seen("203.0.113.30", "dok", t0 + 1) == 0,
              "другая причина — отдельная запись");
        CHECK(rf_seen("203.0.113.30", "never", t0 + 301) == 0,
              "окно истекло — снова записан");
    }
        state_free(&st);
    }

    printf("=== starvation: пачки коротких коннектов (этап 2, только счёт) ===\n");
    {
        time_t now = 1700000000;
        ct_flow s;
        int i;
        memset(&s, 0, sizeof(s));
        snprintf(s.dst, sizeof(s.dst), "%s", "198.51.100.7");
        s.l4proto = 6; s.dport = 443; s.rp = 3; s.ob = 900; s.rb = 400;
        snprintf(s.tcp_state, sizeof(s.tcp_state), "%s", "CLOSE_WAIT");
        memset(starve_tab, 0, sizeof(starve_tab));
        s.sport = 40000;
        starve_note(&s, now);   /* первый коннект */
        starve_note(&s, now + 1);
        starve_note(&s, now + 2);
        for (i = 0; i < STARVE_MAX; i++)
            if (starve_tab[i].key[0] && !strcmp(starve_tab[i].key, "198.51.100.7:443"))
                break;
        CHECK(i < STARVE_MAX && starve_tab[i].n == 1,
              "повторы того же sport — один коннект, не три");
        s.sport = 40001;
        starve_note(&s, now + 3);
        s.sport = 40002;
        starve_note(&s, now + 4);
        CHECK(starve_tab[i].n == 3, "три разных sport в окне — коротыш посчитан");
        snprintf(s.tcp_state, sizeof(s.tcp_state), "%s", "ESTABLISHED");
        s.sport = 40003;
        starve_note(&s, now + 5);
        CHECK(starve_tab[i].n == 3, "живой (ESTABLISHED) коннект не считается");
        snprintf(s.tcp_state, sizeof(s.tcp_state), "%s", "CLOSE_WAIT");
        starve_note(&s, now + STARVE_WIN + 10);
        CHECK(starve_tab[i].n == 1, "новое окно — счёт с нуля");
    }

    printf("=== бюджет проб: сильные ретранслиты SYM идут без пробы ===\n");
    {
        ct_flow s;
        memset(&s, 0, sizeof(s));
        s.l4proto = 6;
        cfg.fast_syn_min_op = 2;
        s.op = 3;
        CHECK(!syn_evidence_strong(&cfg, &s), "op=3 при пороге 4: проба нужна");
        s.op = 4;
        CHECK(syn_evidence_strong(&cfg, &s), "op=4 при пороге 4: проба не нужна");
        cfg.fast_syn_min_op = 3;
        s.op = 5;
        CHECK(!syn_evidence_strong(&cfg, &s), "fast_syn_min_op=3: порог 6, op=5 мала");
        s.op = 6;
        CHECK(syn_evidence_strong(&cfg, &s), "op=6 при пороге 6: без пробы");
        cfg.fast_syn_min_op = 1;
        s.op = 3;
        CHECK(!syn_evidence_strong(&cfg, &s), "пол 4: op=3 всё ещё мала");
        s.op = 4;
        CHECK(syn_evidence_strong(&cfg, &s), "пол 4: op=4 достаточно");
    }

    printf("\n%s\n", failures ? "ЕСТЬ ОШИБКИ" : "все проверки пройдены");
    return failures ? 1 : 0;
}
