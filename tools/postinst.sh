#!/bin/sh
# postinst.sh — ЕДИНСТВЕННАЯ реализация «путей и сервиса» Susanin.Keenetic.
#
# Его выполняют два пути установки:
#   - opkg: как maintainer-скрипт ./postinst из control.tar.gz (install/upgrade);
#   - install.sh в tar-режиме: `sh $PREFIX/tools/postinst.sh` после раскладки
#     файлов (раньше tar-ветка делала всё это своими строками, и две реализации
#     разошлись: миграции ключей и disk_mode были только в install.sh).
#
# Правила:
#   - данные пользователя (конфиг, списки, состояние, профили) НЕ перезаписываем;
#   - идемпотентно: повторный запуск ничего не меняет;
#   - ошибка не валит установку: opkg считает ненулевой код сбоем пакета, поэтому
#     здесь нет `set -e`, а внешние вызовы обёрнуты в `|| true`.
#
# Переменные окружения (нужны install.sh, opkg их не задаёт):
#   SUSANIN_PREFIX      — префикс вместо /opt/susanin (тесты, нестандартный Entware);
#   SUSANIN_EGRESS/LAN/SUBNETS — значения, уже определённые установщиком;
#   SUSANIN_DISK_MODE   — disk_mode, посчитанный установщиком;
#   SUSANIN_KEEP_LISTS  — 1 (--keep-lists): существующие списки не трогаем вовсе.
set -u

PREFIX="${SUSANIN_PREFIX:-/opt/susanin}"
AGENT="$PREFIX/bin/susanin-agent"
CONF="$PREFIX/etc/susanin.conf"
SHARE="$PREFIX/share"

say() { echo "[susanin] $*"; }

# opkg запускает maintainer-скрипты с узким PATH — busybox `ip` ищем явно.
IPBIN=""
if command -v ip >/dev/null 2>&1; then
	IPBIN=$(command -v ip)
else
	for _p in /bin/ip /sbin/ip /opt/bin/ip; do
		[ -x "$_p" ] && IPBIN=$_p && break
	done
fi

# Подсети LAN по connected-маршрутам интерфейсов (тот же приём, что в install.sh).
lan_subnets_of() { # lan_subnets_of <if,if>
	_out=""
	[ -n "$IPBIN" ] || return 0
	for _i in $(printf '%s' "$1" | tr ',' ' '); do
		_p=$("$IPBIN" route show 2>/dev/null |
			awk -v d="$_i" '$0 ~ ("dev " d " ") && $1 ~ /\// {print $1; exit}')
		[ -n "$_p" ] && _out="${_out:+$_out,}$_p"
	done
	printf '%s' "$_out"
}

# --- пути и права (файлы сюда уже положил opkg или install.sh) --------------
mkdir -p "$PREFIX/var" "$PREFIX/var/backup" "$PREFIX/etc/profiles" \
	"$PREFIX/etc/profiles.d" "$SHARE"
chmod 755 "$PREFIX" 2>/dev/null || true
chmod +x "$PREFIX/tools/"*.sh 2>/dev/null || true
[ -f "$AGENT" ] && chmod 755 "$AGENT" 2>/dev/null

# --- конфиг: создаём, только если его ещё нет -------------------------------
_cfg_new=0
if [ ! -f "$CONF" ] && [ -f "$SHARE/susanin.conf.example" ]; then
	cp "$SHARE/susanin.conf.example" "$CONF" &&
		_cfg_new=1 &&
		say "создан конфиг $CONF (можно не править руками: susanin-agent config set)"
fi

cfg_val() { sed -n "s/^$1=[ \t]*//p" "$CONF" 2>/dev/null | tail -n1; }

cfg_set() { # cfg_set <ключ> <значение> <причина>
	[ -f "$CONF" ] || return 0
	if SUSANIN_CONF="$CONF" "$AGENT" config set "$1" "$2" >/dev/null 2>&1; then
		say "авто-детект: $1 → $2 ($3)"
		return 0
	fi
	# Агента нет (первая установка до раскладки бинарника) — пишем сами.
	if grep -q "^$1=" "$CONF" 2>/dev/null; then
		sed -i "s|^$1=.*|$1=$2|" "$CONF" 2>/dev/null || true
	else
		printf '%s=%s\n' "$1" "$2" >>"$CONF"
	fi
	say "авто-детект: $1 → $2 ($3)"
}

# --- отсутствующие ключи -----------------------------------------------------
# Старый susanin.conf мог их не содержать: `sed 's|^key=.*|…|'` на таком ключе
# молча ничего не делает, и веб-панель/Xray кажутся «не включёнными».
ensure_key() { # ensure_key <ключ> <значение>
	[ -f "$CONF" ] || return 0
	grep -q "^$1=" "$CONF" 2>/dev/null || printf '%s=%s\n' "$1" "$2" >>"$CONF"
}
if [ -f "$CONF" ]; then
	ensure_key web_enable 0
	ensure_key web_listen ""
	ensure_key web_port 8087
	ensure_key web_token ""
	ensure_key egress_type interface
	ensure_key tproxy_port 12345
	ensure_key discover_exclude "wdtt0,wdttraw0,tun0,tap0"
	ensure_key fast_syn_min_op 2
	ensure_key ok_max_entries 4096
	ensure_key ok_evict_misses 3
	ensure_key promo_per_min 120
	ensure_key soft_state_interval 12
	ensure_key learn_exclude_ports "22,23,53,135,137,138,139,445,500,554,853,1433,1723,3306,3389,4500,5432,5900,6379,7547,8567,9100,11211,27017"
	ensure_key vpn_always_file /opt/susanin/etc/vpn_always.txt
	ensure_key vpn_always_dns ""
	ensure_key vpn_always_interval 300
	ensure_key vpn_never_file /opt/susanin/etc/vpn_never.txt
	ensure_key vpn_never_interval 300
	ensure_key lan_server_interfaces ""
	ensure_key dp_check_interval 15
	ensure_key learn_min_op 10
	ensure_key learn_min_bytes 2000
	ensure_key confirm_min_bytes 512
	ensure_key learn_strict 0
	ensure_key precision ""
	ensure_key cdn_ranges_file /opt/susanin/etc/cdn_ranges.txt
	ensure_key cdn_ranges_url https://www.cloudflare.com/ips-v4
	ensure_key cdn_ranges_interval 86400
	ensure_key cdn_prefix_learn 1
	ensure_key cdn_prefix_ttl 3600
	ensure_key cdn_prefix_max 24
	ensure_key aggregate_confirm 2
	ensure_key ipv6_block 1
	ensure_key quic_block 1
	ensure_key health_mode icmp
	ensure_key health_tcp_port 443
	ensure_key egress_interface nwg0
	ensure_key egress_address 10.8.1.1
	ensure_key lan_interfaces br0
	ensure_key lan_subnets ""
	ensure_key routing_table 100
	ensure_key mark_test 0x10000000
	ensure_key mark_ok 0x20000000
	ensure_key mark_mask 0x30000000
	ensure_key ip_rule_priority_start 2000
	ensure_key fast_interval 1
	ensure_key soft_interval 1
	ensure_key judge_interval 1
	ensure_key health_interval 5
	ensure_key ok_ttl 21600
	ensure_key ok_refresh_below 3
	ensure_key test_ttl 1
	ensure_key cooldown_ttl 5
	ensure_key cooldown_ok_ttl 30
	ensure_key watch_ttl 8
	ensure_key watch_retry_below 4
	ensure_key health_probe "1.1.1.1,8.8.8.8"
	ensure_key udp_relay 0
	ensure_key udp_relay_port 1081
	ensure_key socks_addr 127.0.0.1
	ensure_key socks_port 1080
	ensure_key log_level info
	ensure_key xray_loglevel warning
	ensure_key egress_failback 1
	ensure_key egress_failback_debounce 30
	ensure_key egress_race 0
	ensure_key xray_gogc 50
	ensure_key xray_gomemlimit 64MiB
	ensure_key xray_watchdog 1
	ensure_key kernel_offload 0
	ensure_key kernel_egress ""
	ensure_key kernel_offload_max 24
	ensure_key dns_sniff 0
	ensure_key dns_sniff_ttl 300
	ensure_key dns_sniff_iface ""
	ensure_key dns_hijack 0
	ensure_key dns_hijack_target ""
	ensure_key vpn_always_cdn_prefix 0
	ensure_key pin_reassert 1
	ensure_key auto_direct 1
	ensure_key direct_pref_ttl 3600
	ensure_key media_enabled 0
	ensure_key media_ports "80,443,554,1935,8080,8443"
	ensure_key media_min_bytes 1048576
	ensure_key media_ratio 8
	ensure_key media_min_rate 150000
	ensure_key media_min_age 12
	ensure_key media_ttl 21600
	ensure_key media_prefix_max 24
	ensure_key mss_clamp 0
	ensure_key mss_clamp_lan 0
	ensure_key profile_failover 1
fi

# --- авто-детект ------------------------------------------------------------
# susanin.conf.example содержит значения «из другой коробки»: egress nwg0,
# lan br0,br1, подсеть 192.168.1.0/24. Голый `opkg install pkg.ipk` на роутере с
# OpenConnect-коннектором давал службу с несуществующим egress и чужой
# подсетью: from_lan() отсекает весь трафик клиентов, и обучение молчит
# (регресс 2026-10-24 разобрали именно на этом). Правим ТОЛЬКО значения,
# которые заведомо никуда не ведут; своё значение пользователя остаётся.
autodetect() {
	[ -f "$CONF" ] || return 0
	_d=""
	if [ -x "$AGENT" ]; then
		_d=$(SUSANIN_CONF="$CONF" "$AGENT" discover 2>/dev/null)
	fi
	[ -n "$_d" ] || return 0

	# egress: интерфейса из конфига нет в системе. tproxy — псевдоимя Xray,
	# его в /sys/class/net не бывает по замыслу (egress_is_tproxy).
	_eg=$(cfg_val egress_interface)
	_d_eg=$(printf '%s\n' "$_d" | sed -n 's/^egress_interface=//p' | head -n1)
	if [ -n "$_d_eg" ] && [ "$_eg" != tproxy ]; then
		if [ -z "$_eg" ] || [ ! -e "/sys/class/net/${_eg%%,*}" ]; then
			cfg_set egress_interface "$_d_eg" "интерфейса «${_eg:-не задан}» нет"
		fi
	fi

	# lan_interfaces: ни одного живого интерфейса из списка.
	_la=$(cfg_val lan_interfaces)
	_d_la=$(printf '%s\n' "$_d" | sed -n 's/^lan_interfaces=//p' | head -n1)
	if [ -n "$_d_la" ] && [ -n "$_la" ]; then
		_alive=0
		for _i in $(printf '%s' "$_la" | tr ',' ' '); do
			[ -e "/sys/class/net/$_i" ] && _alive=1
		done
		[ "$_alive" = 0 ] &&
			cfg_set lan_interfaces "$_d_la" "ни одного живого LAN в «$_la»"
	fi

	# lan_subnets: ни одна подсеть конфига не покрывает фактическую сеть LAN.
	# Значение НЕ берём из discover: discover_defaults() возвращает lan_subnets
	# как есть из конфига (rescan не должен чинить то, что задал пользователь),
	# поэтому подсеть считаем по connected-маршрутам LAN-интерфейсов.
	_ls=$(cfg_val lan_subnets)
	_act=$(lan_subnets_of "$(cfg_val lan_interfaces)")
	if [ -n "$_act" ]; then
		if [ -z "$_ls" ]; then
			cfg_set lan_subnets "$_act" "подсети LAN не заданы"
		else
			_lsc=$(printf '%s' "$_ls" | tr -d ' \t')
			_cover=0
			for _s in $(printf '%s' "$_act" | tr ',' ' '); do
				case ",$_lsc," in *",$_s,"*) _cover=1 ;; esac
			done
			[ "$_cover" = 0 ] &&
				cfg_set lan_subnets "$_act" "«$_ls» не покрывает $_act"
		fi
	fi
}
autodetect

# Значения, которые установщик определил сам (tar-режим), сильнее авто-детекта.
[ -n "${SUSANIN_EGRESS:-}" ] && cfg_set egress_interface "$SUSANIN_EGRESS" "определено установщиком"
[ -n "${SUSANIN_LAN:-}" ] && cfg_set lan_interfaces "$SUSANIN_LAN" "определено установщиком"
[ -n "${SUSANIN_SUBNETS:-}" ] && cfg_set lan_subnets "$SUSANIN_SUBNETS" "определено установщиком"

# --- disk_mode: только если ключа нет ---------------------------------------
# Раньше install.sh перезаписывал disk_mode при каждом запуске, молча стирая
# выбор пользователя (soft/normal). Смысл поля — соответствовать носителю /opt,
# а он между установками не меняется.
disk_mode_guess() {
	_mnt=""
	_fst=""
	_dev=""
	if command -v findmnt >/dev/null 2>&1; then
		_mnt=$(findmnt -n -o TARGET --target /opt 2>/dev/null | head -n1)
		_fst=$(findmnt -n -o FSTYPE --target /opt 2>/dev/null | head -n1)
		_dev=$(findmnt -n -o SOURCE --target /opt 2>/dev/null | head -n1)
	fi
	if [ -z "$_mnt" ] && [ -r /proc/mounts ]; then
		_line=$(grep -E ' /opt ' /proc/mounts 2>/dev/null | head -n1)
		if [ -n "$_line" ]; then
			_dev=$(printf '%s' "$_line" | awk '{print $1}')
			_fst=$(printf '%s' "$_line" | awk '{print $3}')
			_mnt=/opt
		fi
	fi
	if [ "$_mnt" != "/opt" ]; then
		echo soft
		return
	fi
	case "$_fst" in
	ubifs | squashfs | jffs2 | overlay | ramfs | tmpfs)
		echo soft
		return
		;;
	esac
	case "$_dev" in
	*mtdblock* | *ubiblock* | *overlay* | *rootfs* | /storage*)
		echo soft
		return
		;;
	esac
	echo normal
}
if [ -f "$CONF" ] && { [ "$_cfg_new" = 1 ] || [ -z "$(cfg_val disk_mode)" ]; }; then
	cfg_set disk_mode "${SUSANIN_DISK_MODE:-$(disk_mode_guess)}" "носитель /opt"
fi

# --- списки: создаём, при обновлении дополняем ------------------------------
# Дополняем ТОЛЬКО отсутствующими строками: порядок и правки пользователя целы,
# комментарии из пакета не переносим, перед изменением — один бэкап `.bak`.
merge_list() { # merge_list <файл пользователя> <файл пакета> <метка>
	_u="$1"
	_p="$2"
	_lbl="$3"
	[ -f "$_u" ] && [ -f "$_p" ] || return 0
	_tmp="$_u.merge.$$"
	cp "$_u" "$_tmp" 2>/dev/null || return 0
	_added=0
	_hdr=0
	while IFS= read -r _line; do
		case "$_line" in '' | '#'*) continue ;; esac
		grep -qxF "$_line" "$_tmp" 2>/dev/null && continue
		if [ "$_hdr" -eq 0 ]; then
			printf '\n# === добавлено при обновлении пакета (%s) ===\n' "$_lbl" >>"$_tmp"
			_hdr=1
		fi
		printf '%s\n' "$_line" >>"$_tmp"
		_added=$((_added + 1))
	done <"$_p"
	if [ "$_added" -gt 0 ]; then
		cp -f "$_u" "$_u.bak" 2>/dev/null || true
		mv -f "$_tmp" "$_u"
		say "$_lbl: +$_added записей (merge), бэкап $_lbl.txt.bak"
	else
		rm -f "$_tmp"
	 fi
}
# --keep-lists (установщик): существующие списки не трогаем вовсе, merge не делаем.
if [ "${SUSANIN_KEEP_LISTS:-0}" = 1 ]; then
	say "--keep-lists: списки vpn_always/vpn_never не трогаем"
fi
for _l in vpn_always vpn_never; do
	if [ ! -f "$PREFIX/etc/$_l.txt" ]; then
		if [ -f "$SHARE/$_l.txt" ]; then
			cp "$SHARE/$_l.txt" "$PREFIX/etc/$_l.txt"
			say "список: $PREFIX/etc/$_l.txt"
		else
			: >"$PREFIX/etc/$_l.txt"
		fi
	fi
	[ "${SUSANIN_KEEP_LISTS:-0}" = 1 ] && continue
	merge_list "$PREFIX/etc/$_l.txt" "$SHARE/$_l.txt" "$_l"
done
rm -f "$PREFIX/etc/vpn_always.txt".merge.* "$PREFIX/etc/vpn_never.txt".merge.* 2>/dev/null || true
# Бэкапы списков старого формата (<список>.bak-ГГГГММДД-ЧЧММСС): правки через
# панель плодили десятки копий, теперь бэкап один.
rm -f "$PREFIX/etc/vpn_always.txt".bak-[0-9]* \
	"$PREFIX/etc/vpn_never.txt".bak-[0-9]* 2>/dev/null || true

# CDN-диапазоны: файл нужен для агрегации префиксов; первое скачивание — по
# cdn_ranges_url, поэтому пустой файл хуже, чем копия из пакета.
if [ ! -f "$PREFIX/etc/cdn_ranges.txt" ] && [ -f "$SHARE/cdn_ranges.txt" ]; then
	cp "$SHARE/cdn_ranges.txt" "$PREFIX/etc/cdn_ranges.txt"
	say "CDN-диапазоны: $PREFIX/etc/cdn_ranges.txt"
fi

# --- миграции значений по умолчанию -----------------------------------------
# promo_per_min: в 0.4.0 дефолт поднят 30 → 120. Меняем ТОЛЬКО прежний дефолт.
if [ "$(cfg_val promo_per_min)" = "30" ]; then
	cfg_set promo_per_min 120 "новый дефолт 0.4.0"
fi

# --- короткий вызов `susanin …` ---------------------------------------------
if [ -d /opt/bin ] && [ ! -e /opt/bin/susanin ]; then
	ln -s "$PREFIX/tools/susanin.sh" /opt/bin/susanin 2>/dev/null ||
		say "не удалось создать /opt/bin/susanin (не критично)"
fi

# --- сервис ------------------------------------------------------------------
# Init-скрипты в /opt/etc/init.d — часть пакета (или их положил install.sh);
# в Entware сам префикс S означает «автозапуск», отдельной регистрации нет.
if [ "${SUSANIN_NO_START:-0}" = 1 ]; then
	say "запуск службы пропущен (SUSANIN_NO_START=1): sh /opt/etc/init.d/S94susanin start"
else
	# В tproxy-режиме Xray поднимают ДО агента: без слушающего порта агент не
	# ставит tproxy-правила и оставляет трафик в DIRECT (fail-open).
	if [ "$(cfg_val egress_type)" = tproxy ]; then
		if [ -x /opt/sbin/xray ] && [ -f "$PREFIX/etc/xray-tproxy.json" ]; then
			[ -x /opt/etc/init.d/S93xray-tproxy ] &&
				sh /opt/etc/init.d/S93xray-tproxy restart >/dev/null 2>&1 || true
			say "tproxy: Xray поднят перед стартом агента"
		else
			say "ВНИМАНИЕ: egress_type=tproxy, но нет /opt/sbin/xray или $PREFIX/etc/xray-tproxy.json"
			say "         пока Xray не готов, агент оставит трафик в DIRECT (fail-open)"
		fi
	fi
	if [ -x /opt/etc/init.d/S94susanin ]; then
		/opt/etc/init.d/S94susanin restart >/dev/null 2>&1 ||
			say "служба не поднялась: sh /opt/etc/init.d/S94susanin start"
	fi
	# web — отдельный процесс: restart демона его не трогает, а после подмены
	# бинаря панель надо перезапустить, иначе она крутит старый код из памяти.
	if [ -x /opt/etc/init.d/S95susanin-web ]; then
		/opt/etc/init.d/S95susanin-web restart >/dev/null 2>&1 || true
	fi
fi

say "готово. Проверка: susanin status && susanin diagnose"
exit 0
