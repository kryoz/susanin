#ifndef SUSANIN_HEALTH_H
#define SUSANIN_HEALTH_H

#include "config.h"

/* Возвращает 0. *ok — сколько проб получили ответ, *total — сколько отправлено.
 * Источник пробы берётся с самого интерфейса (адрес туннеля динамический: сервер
 * выдаёт его заново при переподключении). src и cfg->egress_address — только
 * запасной вариант для интерфейсов без IPv4 и явного переопределения. */
int health_probe(const susanin_config *c, const char *src, int *ok, int *total);

/* То же, но проба жёстко привязана к интерфейсу (SO_BINDTODEVICE). Нужно для
 * независимой проверки КАЖДОГО egress (master/slave failback): маршрут таблицы
 * 100 указывает на активный egress, поэтому без привязки к устройству проба
 * всегда уходила бы через активный. dev=NULL — обычное поведение. */
int health_probe_dev(const susanin_config *c, const char *dev, const char *src,
                     int *ok, int *total);

/* Прямая TCP-проба dst:port мимо наших правил (по main, без метки):
 * 1 = соединение установилось. */
int health_probe_tcp_direct(const char *dst, unsigned port, int timeout_ms);

/* Страховочная TCP-проба ЧЕРЕЗ egress (dev, src, метка test): нужна, когда ICMP
 * не отвечает, а туннель рабочий (залипшее NAT-состояние на промежуточном
 * сервере каскада по ICMP-ID — отчёт тестера 0.4.0-dev8). 1 = соединение
 * установилось хотя бы с одним адресом из health_probe. */
int health_probe_tcp_egress(const susanin_config *c, const char *dev,
                            const char *src, int port);

/* Проба апстрима Xray через локальный SOCKS5 (режим tproxy): 1 = ok. */
int health_probe_via_socks(const susanin_config *c, const char *dst, int port,
                           int timeout_ms);

#endif
