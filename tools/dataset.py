#!/usr/bin/env python3
"""dataset.py — собрать датасет автообучения из логов агента (этап 1).

Вход — логи `var/susanin.log` (обычные или .gz, сколько угодно, с разных боксов и
за разные сутки), выход — CSV, где на каждую строку решений один ряд: вектор
признаков из `TRC` плюс исход, найденный склейкой по (адрес, порт).

Исход бывает трёх видов, и третий считается здесь, а не в логе:
  CONFIRMED / COOLDOWN — вердикт judge;
  TIMEOUT — промоушен, по которому в окне `--window` вердикта не было (в логе
  такой исход есть только агрегатом TEST-TIMEOUT, по адресам он не разложен).

Строки без даты в метке времени (лог до правки формата) не парсятся: их невозможно
разложить по суткам; число считается в сводке.

Usage:
  python3 tools/dataset.py ~/susanin-logs/*/*.log.gz --summary
  python3 tools/dataset.py live.log -o dataset.csv
  python3 tools/dataset.py --selftest
"""

import argparse
import csv
import gzip
import re
import sys
from collections import Counter
from datetime import datetime

# "10-30 19:30:52 DBG: " — дата обязательна (см. log.c).
TS_RE = re.compile(r"^(\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2}) ")

# TRC sig=FAST/TCP-SYN dst=1.2.3.4 pr=tcp st=SYN_SENT dp=443 sp=1 op=3 ob=180
#    rp=0 rb=0 hr=0 fn=1 dom=example.com ref=-
TRC_RE = re.compile(
    r"TRC sig=(?P<stage>[A-Z]+)/(?P<sig>[A-Z0-9-]+) dst=(?P<dst>\S+) "
    r"pr=(?P<pr>\S+) "
    r"st=(?P<st>\S*).*? dp=(?P<dp>\d+) sp=(?P<sp>\d+) op=(?P<op>\d+) "
    r"ob=(?P<ob>\d+) rp=(?P<rp>\d+) rb=(?P<rb>\d+) hr=(?P<hr>\d+) "
    r"fn=(?P<fn>\d+) dom=(?P<dom>\S+)(?: ref=(?P<ref>\S+))?"
)
VERDICT_RE = re.compile(r"AUTO-SUSANIN: (CONFIRMED|COOLDOWN) (\S+)(?: label=(\w+))?")
PROMO_RE = re.compile(r"AUTO-SUSANIN: (FAST|SOFT) ([A-Z0-9-]+) (\S+?)[:.](\d+)$")

# 'ip:port' разбираем в Python: ленивый regex внутри IP цепляется за первую
# точку ('1.2.3.4:443' -> ip='1', port='2'), а якорь по концу строки не годится —
# у вердикта после адреса может идти label=.
KEY_RE = re.compile(r"(.+)[:.](\d{1,5})$")


def split_key(tok):
    m = KEY_RE.match(tok)
    return "%s:%s" % (m.group(1), m.group(2)) if m else tok

FIELDS = [
    "date", "time", "key", "sig", "dom", "pr", "st", "dp", "sp",
    "op", "ob", "rp", "rb", "hr", "fn", "ref", "outcome", "label", "lag_s",
]


def open_log(path):
    return gzip.open(path, "rt", errors="replace") if path.endswith(".gz") \
        else open(path, "rt", errors="replace")


def parse_ts(m, year):
    mon, day, hh, mm, ss = (int(g) for g in m.groups())
    if mon == 12 and year.month == 1:      # лог через Новый год
        year = year.replace(year=year.year - 1)
    return year.replace(month=mon, day=day, hour=hh, minute=mm, second=ss)


def collect(paths):
    """Один проход по всем файлам: промоушены, вердикты, строки без даты."""
    promos, verdicts, skipped = [], [], 0
    for path in paths:
        anchor = datetime.now()
        with open_log(path) as fh:
            for line in fh:
                m = TS_RE.match(line)
                if not m:
                    if line[:2].isdigit() and line[2] == ":":
                        skipped += 1         # старый формат: только время
                    continue
                ts = parse_ts(m, anchor)
                mv = VERDICT_RE.search(line)
                if mv:
                    verdicts.append((ts, split_key(mv.group(2)), mv.group(1),
                                     mv.group(3) or ""))
                    continue
                mt = TRC_RE.search(line)
                if mt:
                    d = mt.groupdict()
                    d["ts"] = ts
                    d["key"] = "%s:%s" % (d["dst"], d["dp"])
                    promos.append(d)
                    continue
                mp = PROMO_RE.search(line)
                if mp:
                    # промоушен без TRC (лог до log_level=trace): исход всё
                    # равно склеивается, просто без вектора признаков
                    promos.append({"ts": ts, "stage": mp.group(1),
                                   "sig": mp.group(2), "dst": mp.group(3),
                                   "dp": mp.group(4), "key": "%s:%s" %
                                   (mp.group(3), mp.group(4)), "dom": "-",
                                   "ref": "-", "pr": "", "st": "", "sp": "",
                                   "op": "", "ob": "", "rp": "", "rb": "",
                                   "hr": "", "fn": ""})
    verdicts.sort(key=lambda v: v[0])
    return promos, verdicts, skipped


def join(promos, verdicts, window, dedupe):
    """К каждому промоушену — первый вердикт по тому же ключу в окне."""
    by_key = {}
    for ts, key, kind, label in verdicts:
        by_key.setdefault(key, []).append((ts, kind, label))
    rows, seen = [], {}
    stats = Counter()
    for p in sorted(promos, key=lambda x: x["ts"]):
        ref = p.get("ref") or "-"
        if ref != "-" and dedupe:
            prev = seen.get((p["key"], ref))
            # отказ по той же причине каждый проход — это одно событие, не N
            if prev and (p["ts"] - prev).total_seconds() <= dedupe:
                stats["collapsed"] += 1
                continue
            seen[(p["key"], ref)] = p["ts"]
        outcome, label, lag = "TIMEOUT", "", ""
        for ts, kind, lbl in by_key.get(p["key"], []):
            dt = (ts - p["ts"]).total_seconds()
            if 0 <= dt <= window:
                outcome, label, lag = kind, lbl, int(dt)
                break
        stats["outcome:" + outcome] += 1
        if ref == "-":
            stats["promoted"] += 1
        else:
            stats["refused:" + ref] += 1
        if p["dom"] not in ("-", ""):
            stats["with_domain"] += 1
        if p["op"] == "":
            stats["no_features"] += 1
        rows.append({
            "date": p["ts"].strftime("%Y-%m-%d"),
            "time": p["ts"].strftime("%H:%M:%S"),
            "key": p["key"], "sig": "%s/%s" % (p["stage"], p["sig"]),
            "dom": p["dom"], "pr": p["pr"], "st": p["st"], "dp": p["dp"],
            "sp": p["sp"], "op": p["op"], "ob": p["ob"], "rp": p["rp"],
            "rb": p["rb"], "hr": p["hr"], "fn": p["fn"], "ref": ref,
            "outcome": outcome, "label": label, "lag_s": lag,
        })
    return rows, stats


def selftest():
    """Склейка на синтетическом логе: подтверждение, отказ, таймаут, дедуп."""
    log = """10-30 10:00:00 DBG: TRC sig=FAST/TCP-SYN dst=1.1.1.1 pr=tcp st=SYN_SENT dp=443 sp=10 op=3 ob=180 rp=0 rb=0 hr=0 fn=1 dom=a.example ref=-
10-30 10:00:20 INFO: AUTO-SUSANIN: CONFIRMED 1.1.1.1:443 label=clean
10-30 10:00:30 INFO: AUTO-SUSANIN: FAST TCP-SYN 8.8.8.8:443
10-30 10:00:40 INFO: AUTO-SUSANIN: CONFIRMED 8.8.8.8:443
10-30 10:01:00 DBG: TRC sig=FAST/TCP-SYN dst=2.2.2.2 pr=tcp st=SYN_SENT dp=443 sp=11 op=3 ob=180 rp=0 rb=0 hr=0 fn=1 dom=- ref=direct
10-30 10:01:01 DBG: TRC sig=FAST/TCP-SYN dst=2.2.2.2 pr=tcp st=SYN_SENT dp=443 sp=12 op=3 ob=180 rp=0 rb=0 hr=0 fn=1 dom=- ref=direct
10-30 10:02:00 DBG: TRC sig=SOFT/UDP dst=3.3.3.3 pr=udp st= dp=443 sp=13 op=9 ob=900 rp=0 rb=0 hr=0 fn=0 dom=- ref=-
10-30 10:40:00 INFO: AUTO-SUSANIN: COOLDOWN 9.9.9.9:443 label=weak
09:00:00 INFO: строка старого формата, без даты
"""
    path = "/tmp/_susanin_dataset_selftest.log"
    with open(path, "w") as fh:
        fh.write(log)
    promos, verdicts, skipped = collect([path])
    rows, stats = join(promos, verdicts, window=90, dedupe=60)
    by = {r["key"]: r for r in rows}
    checks = [
        (skipped == 1, "строка без даты отброшена и посчитана"),
        (len(rows) == 4, "четыре события (повтор отказа свёрнут), а не пять"),
        (by["1.1.1.1:443"]["outcome"] == "CONFIRMED" and
         by["1.1.1.1:443"]["lag_s"] == 20, " CONFIRMED склеен по ключу"),
        (by["8.8.8.8:443"]["outcome"] == "CONFIRMED" and
         by["8.8.8.8:443"]["label"] == "",
         "промоушен без TRC + вердикт без label (формат до правки) склеены"),
        (by["2.2.2.2:443"]["outcome"] == "TIMEOUT" and
         by["2.2.2.2:443"]["ref"] == "direct", "отказ остался без вердикта"),
        (by["3.3.3.3:443"]["outcome"] == "TIMEOUT",
         "нет вердикта в окне -> TIMEOUT"),
        (stats["collapsed"] == 1, "повторы отказа посчитаны"),
        (stats["with_domain"] == 1, "домен прокинут в ряд"),
    ]
    bad = [msg for ok, msg in checks if not ok]
    for _, msg in checks:
        print(("ok   " if msg not in [m for m in bad] else "FAIL ") + msg)
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="*", help="логи .log или .log.gz")
    ap.add_argument("-o", "--out", help="куда положить CSV (по умолчанию stdout)")
    ap.add_argument("--window", type=int, default=90,
                    help="окно склейки вердикта, сек (test_ttl=60 по умолчанию)")
    ap.add_argument("--dedupe", type=int, default=60,
                    help="слиять повторы отказа по одному ключу, сек; 0 = выкл")
    ap.add_argument("--summary", action="store_true", help="сводка в stderr")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        return selftest()
    if not a.logs:
        ap.error("нужен хотя бы один лог (или --selftest)")

    promos, verdicts, skipped = collect(a.logs)
    rows, stats = join(promos, verdicts, a.window, a.dedupe)

    if a.out:
        with open(a.out, "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=FIELDS)
            w.writeheader()
            w.writerows(rows)
        print("dataset: %d рядов -> %s" % (len(rows), a.out), file=sys.stderr)
    else:
        w = csv.DictWriter(sys.stdout, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)

    if a.summary:
        print("=== сводка ===", file=sys.stderr)
        print("строк без даты (пропущено): %d" % skipped, file=sys.stderr)
        print("вердиктов найдено: %d" % len(verdicts), file=sys.stderr)
        for k in sorted(stats):
            print("%-24s %d" % (k, stats[k]), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
