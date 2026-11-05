# Susanin.Keenetic

**Адаптивная VPN-маршрутизация для KeeneticOS (Entware) · Adaptive VPN routing for KeeneticOS · 面向 KeeneticOS 的自适应 VPN 路由**

[![Language: C11](https://img.shields.io/badge/C-11-blue?logo=c&logoColor=white)](src)
[![KeeneticOS 5.1.4-5.1.5](https://img.shields.io/badge/KeeneticOS-5.1.4--5.1.5-orange)](README.md)
[![Entware required](https://img.shields.io/badge/Entware-required-orange)](README.md)
[![Architectures](https://img.shields.io/badge/arch-mipsel%20%7C%20mips%20%7C%20aarch64%20%7C%20armv7%20%7C%20x86__64-lightgrey)](README.md)
[![License](https://img.shields.io/github/license/R17a/Susanin.Keenetic)](LICENSE)
[![Downloads](https://img.shields.io/github/downloads/R17a/Susanin.Keenetic/total)](https://github.com/R17a/Susanin.Keenetic/releases)

Проект не связан и не аффилирован с Keenetic, Amnezia, WireGuard, MikroTik или
авторами упомянутых сторонних проектов. Используйте на свой страх и риск —
сделайте резервную копию конфигурации роутера.

## Зачем этот форк

Основное и оригинальное решение позаимствовано из [R17a/Susanin.Keenetic](https://github.com/R17a/Susanin.Keenetic). Если у вас он работает - значит вам мой форк не нужен.
Данное решение рассчитано на владельца Keenetic с Entware, у которого egress коннектор - **OpenConnect**, поддержки которого в апстриме нет.

<hr>

## 🇷🇺 Русский

Проект сделан по [статье на Habr](https://habr.com/ru/articles/1076620/) и проекту
[Fiark/susanin](https://github.com/Fiark/susanin) (адаптивная VPN-маршрутизация для
MikroTik RouterOS), перенесён на роутеры Keenetic/Entware.

Адаптивная маршрутизация: программа смотрит, какие соединения «не работают» из-за
блокировок, пробует их через VPN, запоминает рабочие и дальше держит их в VPN. Если
VPN недоступен — всё идёт напрямую. Есть и простой список доменов, которые должны
идти через VPN **всегда** (без ручных списков IP — только имена, как в
Keenetic).

**Статус: релиз форка 0.5.0.** Проверено на Keenetic Giga (KN-1012, aarch64) и
Keenetic Viva (MT7621, MIPS), KeeneticOS 5.1.4–5.1.6, Entware; клиентский
OpenConnect-коннектор Keenetic (`nocliN`), WireGuard/AmneziaWG (`nwg*`) и
Xray VLESS+REALITY (`egress_type=tproxy`). Только IPv4.

**Что делает**

- наблюдает за поведением соединений (conntrack), а не за содержимым;
- по «тихим» признакам блокировок (TCP SYN без ответа, QUIC/UDP без reply,
  TCP-stall, DPI-RST) проверяет адрес через VPN и запоминает результат;
- держит подтверждённые направления в VPN с конечным TTL (`ok_ttl`, 6 ч) и снимает
  их сам, если через VPN перестало отвечать;
- списки «всегда через VPN» и «всегда напрямую»: домены, wildcard `*.example.com`,
  CIDR, IP;
- fail-open (нет туннеля — прямой доступ), failover между несколькими VPN и
  автоматический возврат на приоритетный;
- egress — интерфейс (`nwg*`, `wdtt*`, `nocli*`) или Xray/VLESS+REALITY (`egress_type=tproxy`);
- профили маршрутизации: до 4 списков «домены/CIDR → свой туннель»;
- встроенная веб-панель (только LAN) с интерфейсом на русском, английском и
  китайском; по умолчанию выключена; кнопка «Не открывается сайт?» (жалоба, 0.5.0);
- сам восстанавливает правила, если их снёс NDM (окно без правил ≤1 с, 0.5.0), и
  умеет маршрутизировать клиентов OpenConnect-сервера на этом же роутере.

Не является VPN-клиентом: туннель («Другое подключение»: OpenConnect-коннектор,
WireGuard/AmneziaWG) должен уже работать. IPv6 в v1 не поддерживается.

**Требования**

- Keenetic с Entware; пакеты `ipset`, `iptables`, `conntrack`,
  `ca-certificates` + `wget-ssl`;
- работающий VPN-туннель — для этого форка клиентский OpenConnect-коннектор
  (`nocliN`), также WireGuard/AmneziaWG или Xray/VLESS+REALITY;
- в Keenetic **выключена «Маршрутизация DNS»**, включён шифрованный DNS (DoH/DoT) —
  иначе возможны подмена DNS и «YouTube/Claude не открывается».

  ![Маршрутизация DNS отключена в KeeneticOS](DNS_Route.png)
  ![Настроенный шифрованный DNS (DoH/DoT)](DNS.png)

**Быстрый старт**

```sh
opkg update && opkg install ca-certificates wget-ssl
wget -qO- https://raw.githubusercontent.com/R17a/Susanin.Keenetic/main/install.sh | sh -s -- --ipk
susanin version && susanin status && susanin diagnose
```

**Веб-панель (только LAN):** `web_enable=1`, `web_listen=192.168.1.1`,
`web_port=8087` в `/opt/susanin/etc/susanin.conf`, затем
`sh /opt/susanin/tools/susanin.sh web restart` → `http://192.168.1.1:8087/`.
Язык интерфейса — в шапке справа: русский (по умолчанию), английский, китайский.

**Документация**
- [USER_GUIDE.md](USER_GUIDE.md) — руководство: быстрый старт, задачи, настройки;
- [DEPLOY.md](DEPLOY.md) — установка, обновление, удаление, автозапуск;
- [TECHNICAL.RU.md](TECHNICAL.RU.md) — технические детали: как устроено, списки
  доменов, OpenConnect/qWDTT/XRay, профили, CLI, сборка, все параметры конфига;
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) — если что-то не работает, по симптомам;
- [XRAY.md](XRAY.md) — XRay/VLESS-REALITY: совместимость, конфликты, XKeen;
- [CHANGELOG.md](CHANGELOG.md) — что нового по версиям.

<hr>

### Этот форк: OpenConnect-коннектор (0.5.0)

Egress форка — клиентский OpenConnect-коннектор Keenetic («Другие подключения»,
netdev `nocliN`); авто-детект находит его сам. Отличия от WireGuard: Keenetic
поднимает коннектор с `accept-addresses`/`accept-routes` (в main-таблице сотни
префиксов через `nocli1`, а `default` остаётся на WAN), поэтому в таблицу 100
добавляется `default dev nocli1` — единственное, что даёт «всё помеченное → в
туннель». DNS туннеля задаётся в ndm: `ip name-server <peer> vpn on OpenConnect1`.
`oc+` и `dns_hijack` — про OpenConnect-**сервер** на том же роутере, коннектору
они не нужны.

Установка из релизов форка:

```sh
opkg update && opkg install ca-certificates ipset iptables conntrack wget-ssl
wget -qO- https://raw.githubusercontent.com/kryoz/susanin/fix/oc-connector-egress/install.sh \
  | SUSANIN_REPO=kryoz/susanin sh -s -- --ipk --version v0.5.0 --yes
```

#### Кнопка «Не открывается сайт?» (0.5.0)

Если у клиента сайт не открывается (и не открывается через прокси), а панель
показывает «всё нормально» — нажми кнопку рядом с Reset и введи домен (или
IP). Агент разрешит домен, снимет его с `vpn_never`, поставит адреса в
тестовую фазу и разорвёт текущие коннекты клиента — браузер переподключится
уже через туннель, а судья подтвердит исход по факту трафика. Сайт в
`vpn_never` — жалоба отклоняется (сними с never и повтори). CLI-аналог:
`/opt/susanin/bin/susanin-agent complain <домен|ip>`.

#### Свой `domain_prior.txt` из своих логов (офлайн, 0.5.0)

Приор ускоряет повторное заворачивание доменов, которые уже блокировались в
**твоей** сети. Инструмент — `tools/dataset.py` (чистый Python 3, без
зависимостей; в пакет не входит, берётся из репозитория).

1. На роутере включить видимость (на несколько дней наблюдений):

   ```sh
   /opt/susanin/bin/susanin-agent config set log_level trace
   /opt/susanin/bin/susanin-agent config set dns_sniff 1
   sh /opt/susanin/tools/susanin.sh restart
   ```

2. Забрать логи (несколько суток; `trace` даёт ~3 МБ/сут, ротация на роутере
   держит только 3×5 МиБ — затягивать нельзя):

   ```sh
   scp -P 222 root@<роутер>:'/opt/susanin/var/susanin.log*' .
   ```

3. Обучить приор (нужны строки `dns-map:` — они пишутся при включённом
   `dns_sniff`; оценка точности — `--eval-hours`):

   ```sh
   python3 tools/dataset.py susanin.log* --prior domain_prior.txt --eval-hours 12
   ```

4. Результат положить на роутер (перечитывается по mtime, рестарт не нужен):

   ```sh
   scp -P 222 domain_prior.txt root@<роутер>:/opt/susanin/etc/domain_prior.txt
   ```

## 🇬🇧 English

The project is based on the [Habr article](https://habr.com/ru/articles/1076620/) and
on [Fiark/susanin](https://github.com/Fiark/susanin) (adaptive VPN routing for
MikroTik RouterOS), ported to Keenetic/Entware routers.

Adaptive routing: the program watches which connections "do not work" because of
blocking, tries them through the VPN, remembers the working ones and keeps them in
the VPN. If the VPN is unavailable, everything goes direct. There is also a plain
list of domains that must **always** go through the VPN (no manual IP lists —
names only, like in Keenetic).

**Status: release 0.4.2.** Tested on Keenetic Viva (MT7621, MIPS),
KeeneticOS 5.1.4–5.1.5, Entware, WireGuard/AmneziaWG tunnels (`nwg0`/`nwg1`),
Xray VLESS+REALITY (`egress_type=tproxy`), Keenetic Giga/Ultra.

**What it does**

- watches connection behaviour (conntrack), not payload;
- by "silent" signs of blocking (TCP SYN with no answer, QUIC/UDP with no reply,
  TCP-stall, DPI-RST) probes the address through the VPN and remembers the result;
- keeps confirmed destinations in the VPN with a finite TTL (`ok_ttl`, 6 h) and
  removes them by itself when the route through the VPN stops answering;
- "always via VPN" and "always direct" lists: domains, `*.example.com` wildcards,
  CIDR, IP;
- fail-open (no tunnel — direct access), failover between several VPNs and
  automatic return to the preferred one;
- egress is an interface (`nwg*`, `wdtt*`) or Xray/VLESS+REALITY
  (`egress_type=tproxy`);
- routing profiles: up to 4 lists "domains/CIDR → their own tunnel";
- built-in web panel (LAN only) with a Russian, English and Chinese interface;
  disabled by default;
- repairs its own rules if NDM wipes them and can route clients of an OpenConnect
  server on the same router.

It is not a VPN client: the tunnel (WireGuard/AmneziaWG "Additional connection")
must already work. IPv6 is not supported in v1.

**Requirements**

- Keenetic with Entware; packages `ipset`, `iptables`, `conntrack`,
  `ca-certificates` + `wget-ssl`;
- a working VPN tunnel (WireGuard/AmneziaWG) or Xray/VLESS+REALITY;
- Keenetic **"DNS routing" turned off** and encrypted DNS (DoH/DoT) configured —
  otherwise DNS spoofing is possible and "YouTube/Claude does not open".

**Quick start**

```sh
opkg update && opkg install ca-certificates wget-ssl
wget -qO- https://raw.githubusercontent.com/R17a/Susanin.Keenetic/main/install.sh | sh -s -- --ipk
susanin version && susanin status && susanin diagnose
```

**Web panel (LAN only):** `web_enable=1`, `web_listen=192.168.1.1`,
`web_port=8087` in `/opt/susanin/etc/susanin.conf`, then
`sh /opt/susanin/tools/susanin.sh web restart` → `http://192.168.1.1:8087/`.
The interface language is switched in the header on the right: Russian (default),
English, Chinese.

**Documentation** (currently in Russian; translations are on the way)

- [USER_GUIDE.md](USER_GUIDE.md) — guide: quick start, tasks, settings;
- [DEPLOY.md](DEPLOY.md) — install, update, removal, autostart;
- [TECHNICAL.RU.md](TECHNICAL.RU.md) — internals, domain lists,
  OpenConnect/qWDTT/XRay, profiles, CLI, build, all config parameters;
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) — when something does not work, by symptom;
- [XRAY.md](XRAY.md) — XRay/VLESS-REALITY: compatibility, conflicts, XKeen;
- [CHANGELOG.md](CHANGELOG.md) — what changed per version.

<hr>

## 🇨🇳 中文

本项目基于 [Habr 文章](https://habr.com/ru/articles/1076620/) 和
[Fiark/susanin](https://github.com/Fiark/susanin)（面向 MikroTik RouterOS 的自适应
VPN 路由）实现，并移植到 Keenetic/Entware 路由器。

自适应路由：程序观察哪些连接因封锁而「无法工作」，尝试让它们走 VPN，记住可用的
连接并继续让它们走 VPN。如果 VPN 不可用，所有流量都直连。此外还有一份「始终走
VPN」的域名列表（无需手工维护 IP 列表——只写域名，和 Keenetic 一样）。

**状态：0.4.2 版。** 已在 Keenetic Viva（MT7621，MIPS）、KeeneticOS
5.1.4–5.1.5、Entware、WireGuard/AmneziaWG 隧道（`nwg0`/`nwg1`）、
Xray VLESS+REALITY（`egress_type=tproxy`）、Keenetic Giga/Ultra 上验证。

**功能**

- 观察连接的行为（conntrack），而不是内容；
- 依据封锁的「静默」特征（TCP SYN 无响应、QUIC/UDP 无回复、TCP-stall、DPI-RST）
  经 VPN 探测地址并记住结果；
- 以有限 TTL（`ok_ttl`，6 小时）让已确认的方向走 VPN；若经 VPN 不再响应则自行移除；
- 「始终走 VPN」与「始终直连」列表：域名、`*.example.com` 通配、CIDR、IP；
- fail-open（隧道不可用则直连）、多个 VPN 之间的故障切换与自动切回；
- egress 为接口（`nwg*`、`wdtt*`）或 Xray/VLESS+REALITY（`egress_type=tproxy`）；
- 路由配置文件：最多 4 份「域名/CIDR → 指定隧道」列表；
- 内置网页面板（仅限 LAN），界面支持俄语、英语、中文；默认关闭；
- 若 NDM 清除了规则会自动修复，还可把同一台路由器上 OpenConnect 服务器的客户端
  纳入路由。

它不是 VPN 客户端：隧道（WireGuard/AmneziaWG「额外连接」）必须已经在工作。
v1 不支持 IPv6。

**环境要求**

- 已安装 Entware 的 Keenetic；软件包 `ipset`、`iptables`、`conntrack`、
  `ca-certificates` + `wget-ssl`；
- 已工作的 VPN 隧道（WireGuard/AmneziaWG）或 Xray/VLESS+REALITY；
- Keenetic 中**关闭「DNS 路由」**并配置加密 DNS（DoH/DoT）——否则可能出现 DNS
  篡改以及「YouTube/Claude 打不开」。

**快速开始**

```sh
opkg update && opkg install ca-certificates wget-ssl
wget -qO- https://raw.githubusercontent.com/R17a/Susanin.Keenetic/main/install.sh | sh -s -- --ipk
susanin version && susanin status && susanin diagnose
```

**网页面板（仅限 LAN）：** 在 `/opt/susanin/etc/susanin.conf` 中设置
`web_enable=1`、`web_listen=192.168.1.1`、`web_port=8087`，然后执行
`sh /opt/susanin/tools/susanin.sh web restart` → `http://192.168.1.1:8087/`。
界面语言在右上角切换：俄语（默认）、英语、中文。

**文档**（目前为俄语，翻译正在准备中）

- [USER_GUIDE.md](USER_GUIDE.md) — 指南：快速开始、常见任务、设置；
- [DEPLOY.md](DEPLOY.md) — 安装、更新、卸载、开机自启；
- [TECHNICAL.RU.md](TECHNICAL.RU.md) — 技术细节：内部实现、域名列表、
  OpenConnect/qWDTT/XRay、配置文件、CLI、构建、全部配置参数；
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) — 出问题时按症状排查；
- [XRAY.md](XRAY.md) — XRay/VLESS-REALITY：兼容性、冲突、XKeen；
- [CHANGELOG.md](CHANGELOG.md) — 各版本变更。

<hr>

## Благодарности · Acknowledgements · 致谢

Спасибо людям, которые помогали проекту тестированием на своих роутерах, отчётами
с логами, разбором проблем и предложениями: **Gave_Karen**, **pegakmop**, **NJmist**.

Thanks to the people who helped the project — by testing on their routers, sending
logs, analysing problems and suggesting improvements: **Gave_Karen**, **pegakmop**,
**NJmist**.

感谢帮助过项目的朋友——在自己的路由器上测试、提交日志、分析问题并提出改进建议：
**Gave_Karen**、**pegakmop**、**NJmist**。

## Поддержать · Support · 支持

<p align="center">
  <a href="https://www.donationalerts.com/r/dmitriy_r17a"><img src="https://www.donationalerts.com/img/brand/donationalerts.svg" alt="DonationAlerts" height="44"></a>
  &nbsp; &nbsp; &nbsp; &nbsp;
  <a href="https://pay.cloudtips.ru/p/dcbf5f2e"><img src="https://static.tildacdn.com/tild3431-6231-4938-b464-663831306266/Horiz.svg" alt="CloudTips" height="44"></a>
</p>

## Дисклеймер · Disclaimer · 免责声明

Проект не связан и не аффилирован с Keenetic, Amnezia, WireGuard, MikroTik или
авторами упомянутых сторонних проектов. Используйте на свой страх и риск — сделайте
резервную копию конфигурации роутера.

The project is not connected to or affiliated with Keenetic, Amnezia, WireGuard,
MikroTik or the authors of the mentioned third-party projects. Use it at your own
risk — make a backup of your router configuration.

本项目与 Keenetic、Amnezia、WireGuard、MikroTik 及上述第三方项目的作者无关，也未获
其认可。请自行承担使用风险——请备份路由器配置。

## Лицензия · License · 许可证

MIT — см. [LICENSE](LICENSE).
