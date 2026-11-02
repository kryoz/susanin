/* Этап 1: доменный приор (domain_prior.txt) — пометка записей снифера
 * prior_mark() и lookup dns_sniff_prior_of(). dns_sniff.c включается в TU
 * ради static prior_mark. Сокеты не открываются: записи кладутся dns_store
 * напрямую. make test. */
#define _GNU_SOURCE
#include "dns_sniff.c"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok  %s\n", msg); \
    else { printf("FAIL %s\n", msg); failures++; } } while (0)

int main(void)
{
    susanin_config cfg;
    const char *pf = "/tmp/_susanin_domain_prior.txt";
    FILE *f = fopen(pf, "w");

    if (!f) {
        perror("fopen");
        return 1;
    }
    /* Один домен на строку — так файл читает dns_sniff list_match
     * (как vpn_always.txt); score в файле не нужен. */
    fputs("# domain_prior: eTLD+1\n", f);
    fputs("example.com\n", f);
    fputs("*.media.tv\n", f);
    fputs("other.net\n", f);
    fclose(f);
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.domain_prior_file, sizeof(cfg.domain_prior_file), "%s", pf);

    dns_store("a.example.com", "1.2.3.4", 900);
    dns_store("cdn.media.tv", "5.6.7.8", 900);
    dns_store("unrelated.org", "9.9.9.9", 900);

    CHECK(dns_sniff_count() == 3, "три записи в таблице");
    CHECK(dns_sniff_prior_of("1.2.3.4") == 0, "до prior_mark флага нет");
    prior_mark(&cfg);
    CHECK(dns_sniff_prior_of("1.2.3.4") == 1,
          "поддомен example.com помечен по eTLD+1");
    CHECK(dns_sniff_prior_of("5.6.7.8") == 1,
          "wildcard *.media.tv матчится zone_match");
    CHECK(dns_sniff_prior_of("9.9.9.9") == 0, "не listed домен не помечен");
    CHECK(dns_sniff_prior_of("10.0.0.1") == 0, "незнакомый IP -> 0");
    prior_mark(&cfg);
    CHECK(dns_sniff_prior_of("1.2.3.4") == 1, "повторный prior_mark идемпотентен");

    unlink(pf);
    if (failures) {
        printf("FAIL: %d\n", failures);
        return 1;
    }
    printf("все проверки пройдены\n");
    return 0;
}
