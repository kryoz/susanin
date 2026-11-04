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
# 'dns-map: example.com -> 203.0.113.7' — журнал снифера, единственный мост
# IP<->домен для офлайн-анализа (у TRC-промоушенов dom= известен редко).
DNSMAP_RE = re.compile(r"dns-map: (\S+) -> (\S+)\s*$")
COMPLAINT_RE = re.compile(r"COMPLAINT: (\S+) -> (\S+) \((test|REFUSED)")

# 'ip:port' разбираем в Python: ленивый regex внутри IP цепляется за первую
# точку ('1.2.3.4:443' -> ip='1', port='2'), а якорь по концу строки не годится —
# у вердикта после адреса может идти label=.
KEY_RE = re.compile(r"(.+)[:.](\d{1,5})$")


def split_key(tok):
    m = KEY_RE.match(tok)
    return "%s:%s" % (m.group(1), m.group(2)) if m else tok

FIELDS = [
    "date", "time", "key", "sig", "dom", "jdom", "pr", "st", "dp", "sp",
    "op", "ob", "rp", "rb", "hr", "fn", "ref", "outcome", "label", "lag_s",
]


# Многоуровневые публичные суффиксы: без зависимости от PSL хватает короткого
# списка для eTLD+1 (дев-only инструмент, точности >95% достаточно).
MULTI_TLD = {"co.uk", "org.uk", "gov.uk", "ac.uk", "com.au", "net.au", "com.br",
             "co.jp", "co.kr", "co.nz", "com.mx", "com.tr", "com.ua", "com.cn"}


def etld1(domain):
    """a.b.example.co.uk -> example.co.uk; x.example.com -> example.com."""
    d = domain.rstrip(".").split(".")
    if len(d) >= 3 and ".".join(d[-2:]) in MULTI_TLD:
        return ".".join(d[-3:])
    return ".".join(d[-2:]) if len(d) >= 2 else domain


def open_log(path):
    return gzip.open(path, "rt", errors="replace") if path.endswith(".gz") \
        else open(path, "rt", errors="replace")


def parse_ts(m, year):
    mon, day, hh, mm, ss = (int(g) for g in m.groups())
    if mon == 12 and year.month == 1:      # лог через Новый год
        year = year.replace(year=year.year - 1)
    return year.replace(month=mon, day=day, hour=hh, minute=mm, second=ss)


def collect(paths):
    """Один проход по всем файлам: промоушены, вердикты, dns-map, без даты."""
    promos, verdicts, dnsmaps, complaints, skipped = [], [], [], [], 0
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
                mm = DNSMAP_RE.search(line)
                if mm:
                    dnsmaps.append((ts, mm.group(1), mm.group(2)))
                    continue
                mc = COMPLAINT_RE.search(line)
                if mc:
                    complaints.append((ts, mc.group(1), mc.group(2), mc.group(3)))
                    continue
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

    # Промоушен пишется дважды: INFO-строкой и TRC (если трассировка включена).
    # INFO-вариант без признаков нужен только для логов до появления TRC —
    # иначе каждый промоушен считается дважды (замер: promoted 56 при 28 TRC).
    trc_keys = {(p["key"], p["ts"]) for p in promos if p["op"] != ""}
    promos = [p for p in promos
              if p["op"] != "" or
              not any(p["key"] == k and abs((p["ts"] - t).total_seconds()) <= 5
                      for k, t in trc_keys)]
    return promos, verdicts, dnsmaps, complaints, skipped


def join(promos, verdicts, dnsmaps, window, dedupe, lookback=86400):
    """Каждому промоушену — свой вердикт: строка вердикта расходуется один раз
    и достаётся ближайшему предшествующему промоушену того же ключа (иначе один
    CONFIRMED подписывает все повторные пробы адреса, и «точность» на этом
    датасете получается нарисованной). Домен ряда — последняя dns-map запись
    того же IP не позже момента промоушена в окне lookback (кэш клиента и ttl
    снифера короче, окно закрывает редкие резолвы)."""
    promos = sorted(promos, key=lambda x: x["ts"])
    by_key = {}
    for p in promos:
        p["outcome"], p["label"], p["lag"] = "TIMEOUT", "", ""
        by_key.setdefault(p["key"], []).append(p)
    rows, seen = [], {}
    stats = Counter()
    for ts, key, kind, label in sorted(verdicts, key=lambda v: v[0]):
        for p in reversed(by_key.get(key, [])):
            if p["outcome"] != "TIMEOUT":
                continue
            dt = (ts - p["ts"]).total_seconds()
            if 0 <= dt <= window:
                p["outcome"], p["label"], p["lag"] = kind, label, int(dt)
                break
    by_ip = {}
    for ts, dom, ip in dnsmaps:
        by_ip.setdefault(ip, []).append((ts, dom))
    for v in by_ip.values():
        v.sort(key=lambda x: x[0])
    for p in promos:
        ref = p.get("ref") or "-"
        if ref != "-" and dedupe:
            prev = seen.get((p["key"], ref))
            # отказ по той же причине каждый проход — это одно событие, не N
            if prev and (p["ts"] - prev).total_seconds() <= dedupe:
                stats["collapsed"] += 1
                continue
            seen[(p["key"], ref)] = p["ts"]
        jdom = ""
        hist = by_ip.get(p["dst"])
        if hist:
            for ts, dom in reversed(hist):
                if ts <= p["ts"]:
                    if (p["ts"] - ts).total_seconds() <= lookback:
                        jdom = dom
                    break
        stats["outcome:" + p["outcome"]] += 1
        if ref == "-":
            stats["promoted"] += 1
        else:
            stats["refused:" + ref] += 1
        if p["dom"] not in ("-", ""):
            stats["with_domain"] += 1
        if jdom:
            stats["with_joined_domain"] += 1
        if p["op"] == "":
            stats["no_features"] += 1
        rows.append({
            "date": p["ts"].strftime("%Y-%m-%d"),
            "time": p["ts"].strftime("%H:%M:%S"),
            "key": p["key"], "sig": "%s/%s" % (p["stage"], p["sig"]),
            "dom": p["dom"], "jdom": jdom, "pr": p["pr"], "st": p["st"],
            "dp": p["dp"], "sp": p["sp"], "op": p["op"], "ob": p["ob"],
            "rp": p["rp"], "rb": p["rb"], "hr": p["hr"], "fn": p["fn"],
            "ref": ref, "outcome": p["outcome"], "label": p["label"],
            "lag_s": p["lag"], "_ts": int(p["ts"].timestamp()),
        })
    return rows, stats

def build_prior(rows, min_conf=2):
    """Приор по eTLD+1: CONFIRMED — за; COOLDOWN и отказы dok/direct/budget —
    против (прямая жива или бюджет). TIMEOUT не считаем ничем: часть из них —
    закрытое окно клиента, а не доказательство «прямо живо». Сглаживание —
    Лаплас: score=(conf+1)/(conf+neg+2)."""
    st = {}
    for r in rows:
        dom = r.get("jdom") or (r["dom"] if r["dom"] not in ("-", "") else "")
        if not dom:
            continue
        e = st.setdefault(etld1(dom), {"conf": 0, "neg": 0, "ips": set()})
        if r["outcome"] == "CONFIRMED":
            e["conf"] += 1
            e["ips"].add(r["key"].rsplit(":", 1)[0])
        elif r["outcome"] == "COOLDOWN" or r["ref"] in ("dok", "direct", "budget"):
            e["neg"] += 1
    out = []
    for d, e in st.items():
        if e["conf"] >= min_conf and \
                (e["conf"] + 1.0) / (e["conf"] + e["neg"] + 2.0) > 0.5:
            out.append((d, (e["conf"] + 1.0) / (e["conf"] + e["neg"] + 2.0),
                        e["conf"], e["neg"], len(e["ips"])))
    out.sort(key=lambda x: (-x[1], -x[2], x[0]))
    return out


def evaluate(rows, split_epoch):
    """Офлайн-оценка: приор строится только по рядам до split_epoch, проверяется
    на последующих. hit — доля подтверждений теста, чей eTLD+1 был в приоре."""
    train = [r for r in rows if r["_ts"] < split_epoch]
    test = [r for r in rows if r["_ts"] >= split_epoch]
    prior = {d for d, *_ in build_prior(train)}
    conf = [r for r in test if r["outcome"] == "CONFIRMED"]
    hits = sum(1 for r in conf if r["jdom"] and etld1(r["jdom"]) in prior)
    cov = sum(1 for r in conf if r["jdom"])
    pp = [r for r in test if r["jdom"] and etld1(r["jdom"]) in prior
          and r["ref"] == "-"]
    pp_ok = sum(1 for r in pp if r["outcome"] == "CONFIRMED")
    return {"test_conf": len(conf), "prior_hit": hits, "domain_cov": cov,
            "prior_promos": len(pp), "prior_conf": pp_ok,
            "prior_size": len(prior), "train_rows": len(train),
            "test_rows": len(test)}


def selftest():
    """Склейка на синтетическом логе: подтверждение, отказ, таймаут, дедуп."""
    log = """10-29 09:00:00 DBG: dns-map: stale.example -> 1.1.1.1
10-30 09:50:00 INFO: COMPLAINT: broken.example -> 1.1.1.1 (test)
10-30 09:55:00 DBG: dns-map: a.example.com -> 1.1.1.1
10-30 10:00:00 DBG: TRC sig=FAST/TCP-SYN dst=1.1.1.1 pr=tcp st=SYN_SENT dp=443 sp=10 op=3 ob=180 rp=0 rb=0 hr=0 fn=1 dom=a.example ref=-
10-30 10:00:20 INFO: AUTO-SUSANIN: CONFIRMED 1.1.1.1:443 label=clean
10-30 10:00:30 INFO: AUTO-SUSANIN: FAST TCP-SYN 8.8.8.8:443
10-30 10:00:40 INFO: AUTO-SUSANIN: CONFIRMED 8.8.8.8:443
10-30 10:01:00 DBG: TRC sig=FAST/TCP-SYN dst=2.2.2.2 pr=tcp st=SYN_SENT dp=443 sp=11 op=3 ob=180 rp=0 rb=0 hr=0 fn=1 dom=- ref=direct
10-30 10:01:01 DBG: TRC sig=FAST/TCP-SYN dst=2.2.2.2 pr=tcp st=SYN_SENT dp=443 sp=12 op=3 ob=180 rp=0 rb=0 hr=0 fn=1 dom=- ref=direct
10-30 10:02:00 DBG: TRC sig=SOFT/UDP dst=3.3.3.3 pr=udp st= dp=443 sp=13 op=9 ob=900 rp=0 rb=0 hr=0 fn=0 dom=- ref=-
10-30 10:03:00 DBG: TRC sig=FAST/TCP-SYN dst=4.4.4.4 pr=tcp st=SYN_SENT dp=443 sp=14 op=3 ob=180 rp=0 rb=0 hr=0 fn=1 dom=- ref=-
10-30 10:03:40 DBG: TRC sig=FAST/TCP-SYN dst=4.4.4.4 pr=tcp st=SYN_SENT dp=443 sp=15 op=3 ob=180 rp=0 rb=0 hr=0 fn=1 dom=- ref=-
10-30 10:03:50 INFO: AUTO-SUSANIN: CONFIRMED 4.4.4.4:443 label=clean
10-30 10:04:00 INFO: AUTO-SUSANIN: FAST TCP-SYN 5.5.5.5:443
10-30 10:04:00 DBG: TRC sig=FAST/TCP-SYN dst=5.5.5.5 pr=tcp st=SYN_SENT dp=443 sp=16 op=3 ob=180 rp=0 rb=0 hr=0 fn=1 dom=- ref=-
10-30 10:40:00 INFO: AUTO-SUSANIN: COOLDOWN 9.9.9.9:443 label=weak
09:00:00 INFO: строка старого формата, без даты
"""
    path = "/tmp/_susanin_dataset_selftest.log"
    with open(path, "w") as fh:
        fh.write(log)
    promos, verdicts, dnsmaps, complaints, skipped = collect([path])
    rows, stats = join(promos, verdicts, dnsmaps, window=90, dedupe=60,
                       lookback=86400)
    by = {(r["key"], r["time"]): r for r in rows}
    checks = [
        (skipped == 1, "строка без даты отброшена и посчитана"),
        (len(complaints) == 1 and complaints[0][1] == "broken.example",
         "жалоба распарсена и учтена"),
        (len(rows) == 7, "семь событий: двойная запись промоушена свёрнута"),
        (sum(1 for r in rows if r["key"] == "5.5.5.5:443") == 1 and
         by[("5.5.5.5:443", "10:04:00")]["op"] == "3",
         "INFO+TRC одного промоушена — один ряд, с признаками"),
        (by[("8.8.8.8:443", "10:00:30")]["op"] == "",
         "INFO без парного TRC (формат до правки) не выбрасывается"),
        (by[("1.1.1.1:443", "10:00:00")]["outcome"] == "CONFIRMED" and
         by[("1.1.1.1:443", "10:00:00")]["lag_s"] == 20, " CONFIRMED склеен по ключу"),
        (by[("8.8.8.8:443", "10:00:30")]["outcome"] == "CONFIRMED" and
         by[("8.8.8.8:443", "10:00:30")]["label"] == "",
         "промоушен без TRC + вердикт без label (формат до правки) склеены"),
        (by[("2.2.2.2:443", "10:01:00")]["outcome"] == "TIMEOUT" and
         by[("2.2.2.2:443", "10:01:00")]["ref"] == "direct",
         "отказ остался без вердикта"),
        (by[("3.3.3.3:443", "10:02:00")]["outcome"] == "TIMEOUT",
         "нет вердикта в окне -> TIMEOUT"),
        (by[("4.4.4.4:443", "10:03:40")]["outcome"] == "CONFIRMED" and
         by[("4.4.4.4:443", "10:03:00")]["outcome"] == "TIMEOUT",
         "одна строка вердикта подписывает только ближайшую пробу"),
        (stats["collapsed"] == 1, "повторы отказа посчитаны"),
        (stats["with_domain"] == 1, "домен прокинут в ряд"),
        (by[("1.1.1.1:443", "10:00:00")]["jdom"] == "a.example.com" and
         by[("2.2.2.2:443", "10:01:00")]["jdom"] == "",
         "домен склеен по IP из dns-map, устаревшая запись за окном отброшена"),
        (any(d == "example.com" for d, *_ in build_prior(rows, min_conf=1)),
         "приор по eTLD+1 собирается из подтверждений"),
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
    ap.add_argument("--prior", metavar="FILE",
                    help="выгрузить доменный приор: eTLD+1 score conf neg ips")
    ap.add_argument("--lookback", type=int, default=86400,
                    help="окно склейки IP->домен по dns-map, сек (сутки)")
    ap.add_argument("--eval-hours", type=int, default=0, metavar="H",
                    help="офлайн-оценка приора: H часов на обучение, остаток — тест")
    a = ap.parse_args()

    if a.selftest:
        return selftest()
    if not a.logs:
        ap.error("нужен хотя бы один лог (или --selftest)")

    promos, verdicts, dnsmaps, complaints, skipped = collect(a.logs)
    rows, stats = join(promos, verdicts, dnsmaps, a.window, a.dedupe,
                       a.lookback)
    rows.sort(key=lambda r: r["_ts"])

    if a.eval_hours and rows:
        ev = evaluate(rows, rows[0]["_ts"] + a.eval_hours * 3600)
        print("=== оценка приора (обучение %d ч) ===" % a.eval_hours,
              file=sys.stderr)
        for k in ("train_rows", "test_rows", "prior_size", "test_conf",
                  "prior_hit", "domain_cov", "prior_promos", "prior_conf"):
            print("%-14s %d" % (k, ev[k]), file=sys.stderr)

    if a.prior:
        pr = build_prior(rows)
        with open(a.prior, "w") as fh:
            fh.write("# domain_prior: eTLD+1 с историей подтверждений в VPN.\n"
                     "# Один домен на строку: читает dns_sniff list_match,\n"
                     "# формат как у vpn_always.txt (score здесь не нужен).\n")
            for d, *_ in pr:
                fh.write("%s\n" % d)
        for d, sc, c, n, ips in pr[:20]:
            print("prior %-30s score=%.3f conf=%d neg=%d ips=%d" %
                  (d, sc, c, n, ips), file=sys.stderr)
        print("prior: %d доменов -> %s" % (len(pr), a.prior), file=sys.stderr)

    if a.out:
        with open(a.out, "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=FIELDS, extrasaction="ignore")
            w.writeheader()
            w.writerows(rows)
        print("dataset: %d рядов -> %s" % (len(rows), a.out), file=sys.stderr)
    else:
        w = csv.DictWriter(sys.stdout, fieldnames=FIELDS, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    if a.summary:
        print("жалоб (COMPLAINT): %d" % len(complaints), file=sys.stderr)
        print("=== сводка ===", file=sys.stderr)
        print("строк без даты (пропущено): %d" % skipped, file=sys.stderr)
        print("вердиктов найдено: %d" % len(verdicts), file=sys.stderr)
        print("dns-map пар: %d" % len(dnsmaps), file=sys.stderr)
        for k in sorted(stats):
            print("%-24s %d" % (k, stats[k]), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
