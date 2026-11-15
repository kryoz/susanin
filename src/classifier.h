#ifndef SUSANIN_CLASSIFIER_H
#define SUSANIN_CLASSIFIER_H

#include "config.h"
#include "conntrack.h"
#include "state.h"
#include <time.h>

typedef struct {
    const susanin_config *cfg;
    susanin_state *st;
    /* Когда туннель в последний раз падал (0 = не падал с запуска). Вердикт
     * judge, взятый в окне test_ttl после падения, не отличает «адрес не
     * блокируется» от «туннель умер во время пробы» — такая метка помечается
     * weak и в обучение не попадает. */
    time_t tunnel_down_at;
} classifier_ctx;

void clr_fast(classifier_ctx *ctx, const ct_flow *flows, int n, time_t now);
void clr_soft(classifier_ctx *ctx, const ct_flow *flows, int n, time_t now);
void clr_judge(classifier_ctx *ctx, const ct_flow *flows, int n, time_t now);

/* Строка признаков промоушена/отказа для датасета (см. classifier.c). Вызывается
 * только при включённом трассировщике; вынесена в экспорт ради теста. */
void clf_trace_line(char *dst, size_t n, const ct_flow *f, const char *stage,
                    const char *reason, const char *domain);

/* Чистые фильтры обучения (без форков) — нужны движку в sweep_direct(). */
int clf_service_port(unsigned port);
int clf_port_excluded(const susanin_config *cfg, unsigned port);
int clf_is_private_dst(const char *dst);
/* Бинарные варианты для горячего пути: берут готовый u32 из ct_flow
 * (src4/dst4; 0 = адрес не IPv4) и кэш lan_subnets — без строкового разбора
 * и inet_pton на каждый поток в каждом проходе. */
int clf_from_lan4(const susanin_config *cfg, uint32_t src4);
int clf_is_private4(uint32_t a);
/* Число событий обучения (промоушены/подтверждения/кулдауны/взятие под
 * наблюдение) с прошлого вызова; сбрасывает счётчик. Движок по нему выбирает
 * период сканирования conntrack: долго нет событий — сканируем реже. */
unsigned clf_take_events(void);
/* 1 = адрес ip входит в cidr ("a.b.c.d/len"). Нужен reset/forget, чтобы снять
 * покрывающий префикс из susanin_ok_net (/24 оставался). */
int clf_ip_in_cidr(const char *ip, const char *cidr);

#endif
