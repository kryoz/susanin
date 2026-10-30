#ifndef SUSANIN_VERSION_H
#define SUSANIN_VERSION_H

#define SUSANIN_VERSION "0.4.2-oc"
/* Суффикс -oc = сборки этого форка под OpenConnect-коннектор Keenetic: тег
 * v0.4.2-oc, пакет susanin-keenetic_0.4.2-oc_<arch>.ipk. В opkg/dpkg-семантике
 * 0.4.0 < 0.4.0-oc < 0.4.1 < 0.4.1-oc < 0.4.2 < 0.4.2-oc: апстримный релиз без
 * ревизии стоит ниже той же версии с «oc», поэтому апстримный 0.4.2 встанет
 * поверх только с --force-downgrade (0.4.3 и выше — как обычное обновление). */

#endif
