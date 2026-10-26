#ifndef SUSANIN_VERSION_H
#define SUSANIN_VERSION_H

#define SUSANIN_VERSION "0.4.1-oc"
/* Суффикс -oc = сборки этого форка под OpenConnect-коннектор Keenetic: тег
 * v0.4.1-oc, пакет susanin-keenetic_0.4.1-oc_<arch>.ipk. В opkg/dpkg-семантике
 * 0.4.0 < 0.4.0-oc < 0.4.1 < 0.4.1-oc: пустая ревизия у апстрима меньше нашей
 * «oc», поэтому апстримный 0.4.1 встанет поверх только с --force-downgrade
 * (0.4.2 и выше — как обычное обновление). */

#endif
