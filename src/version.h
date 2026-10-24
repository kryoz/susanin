#ifndef SUSANIN_VERSION_H
#define SUSANIN_VERSION_H

#define SUSANIN_VERSION "0.4.0-oc"
/* Суффикс -oc = сборки этого форка под OpenConnect-коннектор Keenetic: тег
 * v0.4.0-oc, пакет susanin-keenetic_0.4.0-oc_<arch>.ipk. В opkg/dpkg-семантике
 * 0.4.0 < 0.4.0-oc < 0.4.1, поэтому апстрим-пакет 0.4.0 не затрёт нашу сборку
 * (opkg сочтёт его понижением), а следующий релиз апстрима останется новее. */

#endif
