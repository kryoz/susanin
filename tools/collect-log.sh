#!/bin/sh
# collect-log.sh — снять лог агента с роутера на машину, где живёт cron (домашний
# сервер), и хранить снимками по 6 часов. Нужен для датасета автообучения: лог на
# роутере режется ротацией (5 МиБ, 3 архива ≈ 4 суток), поэтому без внешнего
# сбора история датасета теряется.
#
# Запускается НЕ на роутере и в поставку не входит (packaging/dev-only.txt).
# Ключ на роутере ограничен command="cat /opt/susanin/var/susanin.log" — этим
# ключом нельзя ничего, кроме чтения одного файла.
#
# Usage:
#   sh collect-log.sh                                  # один снимок
#   ROUTER=root@192.168.0.1 PORT=222 sh collect-log.sh # другой роутер
#   KEEP=14 DIR=/var/lib/susanin-logs sh collect-log.sh
set -eu

ROUTER="${ROUTER:-root@192.168.0.1}"
PORT="${PORT:-222}"
KEY="${KEY:-$HOME/.ssh/susanin_log_id}"
DIR="${DIR:-$HOME/susanin-logs}"
KEEP="${KEEP:-30}" # дней хранения снимков

mkdir -p "$DIR"
tmp="$DIR/.incoming.$$"
trap 'rm -f "$tmp"' INT TERM EXIT

if ! ssh -p "$PORT" -i "$KEY" -o BatchMode=yes -o ConnectTimeout=10 -n -T "$ROUTER" > "$tmp"; then
    echo "collect-log: не удалось снять лог с $ROUTER" >&2
    exit 1
fi
if [ ! -s "$tmp" ]; then
    echo "collect-log: пустой ответ (лог не читается?)" >&2
    exit 1
fi

# Повтор того же снимка пропускается: сравниваем BYTES ДО упаковки — gzip пишет в
# заголовок mtime, поэтому хэши двух одинаковых по содержимому .gz не совпадают.
newest=$(ls -1t "$DIR"/*/*.log.gz 2>/dev/null | head -1 || true)
if [ -n "$newest" ] && [ "$(gunzip -c "$newest" | md5sum)" = "$(md5sum < "$tmp")" ]; then
    echo "collect-log: без изменений ($(basename "$newest"))"
    exit 0
fi

day="$DIR/$(date +%F)"
out="$day/$(date +%H%M).log.gz"
mkdir -p "$day"
gzip -c < "$tmp" > "$out"
echo "collect-log: $(wc -c < "$tmp" | tr -d " ") байт -> $out"

# снимки старше KEEP дней выбрасываются вместе с каталогом дня
cut=$(date -d "-$KEEP day" +%F 2>/dev/null || date -v-"${KEEP}"d +%F)
for d in "$DIR"/[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]; do
    [ -d "$d" ] || continue
    if [ "$(basename "$d")" \< "$cut" ]; then
        rm -rf "$d"
    fi
done
exit 0
