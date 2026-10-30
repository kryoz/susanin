/* Тест разбора L2-кадра для dns_sniff: сокет SOCK_RAW отдаёт кадр целиком, а
 * LAN-порты Keenetic — VLAN-сабы (eth2.1, ra7.1), так что ethertype лежит не
 * по смещению 12. dns_sniff.c включается в TU ради static l2_ipv4. make test. */
#define _GNU_SOURCE
#include "dns_sniff.c"
#include "dns_sniff.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok  %s\n", msg); \
    else { printf("FAIL %s\n", msg); failures++; } } while (0)

/* Свои имена: ETH_P_* уже определены в <linux/if_ether.h>, который тянет
 * dns_sniff.c, и переопределение — ошибка сборки. */
#define T_IPV4   0x0800
#define T_ARP    0x0806
#define T_IPV6   0x86dd
#define TPID_1Q  0x8100
#define TPID_AD  0x88a8

static void set16(uint8_t *p, size_t off, uint16_t v)
{
    p[off] = (uint8_t)(v >> 8);
    p[off + 1] = (uint8_t)(v & 0xff);
}

int main(void)
{
    uint8_t f[128];
    unsigned l2 = 0;
    int i;

    printf("=== обычный кадр ===\n");
    memset(f, 0, sizeof(f));
    set16(f, 12, T_IPV4);
    CHECK(l2_ipv4(f, 42, &l2) && l2 == 14, "IPv4 без тегов -> смещение 14");

    printf("=== VLAN (802.1Q) ===\n");
    memset(f, 0, sizeof(f));
    set16(f, 12, TPID_1Q);
    set16(f, 16, T_IPV4);       /* вложенный ethertype идёт после TCI */
    CHECK(l2_ipv4(f, 46, &l2) && l2 == 18, "один тег -> смещение 18");

    printf("=== Q-in-Q (802.1ad + 802.1Q) ===\n");
    memset(f, 0, sizeof(f));
    set16(f, 12, TPID_AD);
    set16(f, 16, TPID_1Q);
    set16(f, 20, T_IPV4);
    CHECK(l2_ipv4(f, 50, &l2) && l2 == 22, "два тега -> смещение 22");

    printf("=== чужие ethertype ===\n");
    memset(f, 0, sizeof(f));
    set16(f, 12, T_ARP);
    CHECK(!l2_ipv4(f, 42, &l2), "ARP не разбираем");
    set16(f, 12, T_IPV6);
    CHECK(!l2_ipv4(f, 42, &l2), "IPv6 не разбираем (проект только IPv4)");
    set16(f, 12, TPID_1Q);
    set16(f, 16, T_ARP);
    CHECK(!l2_ipv4(f, 46, &l2), "ARP под VLAN-тегом не разбираем");

    printf("=== обрезанные кадры (данные из сети) ===\n");
    memset(f, 0, sizeof(f));
    set16(f, 12, T_IPV4);
    CHECK(!l2_ipv4(f, 13, &l2), "кадр короче заголовка Ethernet");
    memset(f, 0, sizeof(f));
    set16(f, 12, TPID_1Q);
    CHECK(!l2_ipv4(f, 16, &l2), "тег обещает продолжения, которых нет");
    CHECK(!l2_ipv4(f, 17, &l2), "тег обрезан на середине поля");

    printf("=== злонамеренная цепочка тегов ===\n");
    memset(f, 0, sizeof(f));
    for (i = 0; i < 10; i++)
        set16(f, (size_t)(12 + 4 * i), TPID_1Q);   /* каждый следующий вложенный */
    CHECK(!l2_ipv4(f, 14 + 4 * 10 - 1, &l2),
          "цепочка тегов упирается в границу буфера, а не читает за ней");
    memset(f, 0, sizeof(f));
    for (i = 0; i < 10; i++)
        set16(f, (size_t)(12 + 4 * i), TPID_AD);
    set16(f, 52, T_IPV4);
    CHECK(l2_ipv4(f, 56, &l2) && l2 == 54, "десяток тегов внутри буфера обработан");

    printf("\n%s\n", failures ? "ЕСТЬ ОШИБКИ" : "все проверки пройдены");
    return failures ? 1 : 0;
}
