/* Тесты «известных значений» для веб-панели (GET /api/choices): JSON-сборка,
 * классификация интерфейсов, разбор `ip -o addr`, разбор конфига, диапазон
 * свободных меток. web_choices.c включается в TU, чтобы дотянуться до
 * static-функций. make test. */
#define _GNU_SOURCE
#include "web_choices.c"
#include "web_choices.h"

#include <assert.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok  %s\n", msg); \
    else { printf("FAIL %s\n", msg); failures++; } } while (0)

static void write_file(const char *p, const char *s)
{
    FILE *f = fopen(p, "wb");
    assert(f);
    fputs(s, f);
    fclose(f);
}

/* Пути для временных деревьев теста. Буфер статический, поэтому вложенные
 * вызовы внутри одного выражения недопустимы — тест использует их раздельно. */
static const char *join_path(const char *dir, const char *name, const char *sub)
{
    static char buf[1200];
    snprintf(buf, sizeof(buf), "%s/%s%s%s", dir, name, sub ? "/" : "", sub ? sub : "");
    return buf;
}

static int mkdir_join(const char *dir, const char *name)
{
    return mkdir(join_path(dir, name, NULL), 0755) == 0;
}

/* Значение элемента по имени (field) или NULL. */
static const char *item_value(const choice_set *cs, const char *name)
{
    size_t i;
    for (i = 0; i < cs->n_ifaces; i++)
        if (!strcmp(cs->ifaces[i].field, name))
            return cs->ifaces[i].value;
    for (i = 0; i < cs->n_lists; i++)
        if (!strcmp(cs->lists[i].field, name))
            return cs->lists[i].value;
    return NULL;
}

/* Есть ли элемент с таким field в массиве. */
static const char *item_field(const choice_item *arr, size_t n, const char *field)
{
    size_t i;
    /* Ищем элемент по ЗНАЧЕНИЮ: тест передаёт "202"/"0x40000000" и т.п.
     * (поле .field — это имя параметра, напр. "profile_mark"). */
    for (i = 0; i < n; i++)
        if (!strcmp(arr[i].value, field))
            return arr[i].value;
    return NULL;
}

/* --- JSON-сборщик ---------------------------------------------------- */

static void test_jbuf(void)
{
    char b[64];
    jbuf j;
    int k;

    printf("=== JSON-сборщик: переполнение не даёт битого ответа ===\n");
    jc_init(&j, b, sizeof(b));
    CHECK(jc_add(&j, "abc") == 1, "короткая строка добавлена");
    CHECK(strcmp(b, "abc") == 0, "буфер заполнен верно");
    for (k = 0; k < 100; k++)
        jc_addf(&j, "%d,", k);
    CHECK(j.full == 1, "переполнение помечено (full)");
    CHECK(j.len + 1 <= sizeof(b), "запись не ушла за границу буфера");
    CHECK(b[j.len] == '\0', "строка терминирована");
    CHECK((size_t)strlen(b) == j.len, "нет «склеенных» хвостов");

    jc_init(&j, b, sizeof(b));
    CHECK(j.len == 0 && !j.full, "jc_init сбрасывает состояние");
    CHECK(jc_add(&j, "x") == 1 && b[0] == 'x' && b[1] == '\0', "запись с начала");
}

static void test_mark_escape(void)
{
    char b[128];
    jbuf j;

    printf("=== JSON: экранирование имён ===\n");
    jc_init(&j, b, sizeof(b));
    jc_addq(&j, "if\"a\\b");
    CHECK(strcmp(b, "\"if\\\"a\\\\b\"") == 0, "кавычка и бэкслеш экранированы");
    jc_init(&j, b, sizeof(b));
    jc_addq(&j, "x\ny\tz");
    CHECK(strcmp(b, "\"x\\ny\\tz\"") == 0, "перевод строки и таб экранированы");
    jc_init(&j, b, sizeof(b));
    jc_addq(&j, "a\x01z");
    CHECK(strcmp(b, "\"a\\u0001z\"") == 0, "управляющий байт -> \\u00xx");
}

/* --- классификация интерфейсов ---------------------------------------- */

static void test_kinds(void)
{
    printf("=== kind интерфейса ===\n");
    CHECK(strcmp(choice_iface_kind("nwg0"), "tunnel") == 0, "nwg0 -> tunnel");
    CHECK(strcmp(choice_iface_kind("wdtt0"), "tunnel") == 0, "wdtt0 -> tunnel");
    CHECK(strcmp(choice_iface_kind("wg1"), "tunnel") == 0, "wg1 -> tunnel");
    /* AmneziaWG (userspace amneziawg-go или нативный awg-интерфейс) — тоже
     * тоннель, иначе в панели он падал в группу «Физические». */
    CHECK(strcmp(choice_iface_kind("awg0"), "tunnel") == 0, "awg0 -> tunnel");
    CHECK(strcmp(choice_iface_kind("awg1"), "tunnel") == 0, "awg1 -> tunnel");
    CHECK(strcmp(choice_iface_kind("tun0"), "tunnel") == 0, "tun0 -> tunnel");
    CHECK(strcmp(choice_iface_kind("ppp0"), "tunnel") == 0, "ppp0 -> tunnel");
    CHECK(strcmp(choice_iface_kind("br0"), "bridge") == 0, "br0 -> bridge");
    CHECK(strcmp(choice_iface_kind("br-lan"), "bridge") == 0, "br-lan -> bridge");
    CHECK(strcmp(choice_iface_kind("eth3"), "physical") == 0, "eth3 -> physical");
    CHECK(strcmp(choice_iface_kind("wlan0"), "physical") == 0, "wlan0 -> physical");
    CHECK(strcmp(choice_iface_kind(""), "physical") == 0, "пустое имя -> physical");
    CHECK(strcmp(choice_iface_kind(NULL), "physical") == 0, "NULL -> physical");

    CHECK(strcmp(choice_dev_kind("brm0", "DEVTYPE=bridge\n"), "bridge") == 0,
          "DEVTYPE=bridge перебивает имя без br");
    CHECK(strcmp(choice_dev_kind("wg0", "DEVTYPE=wireguard\n"), "tunnel") == 0,
          "не-bridge uevent не мешает имени");
    CHECK(strcmp(choice_dev_kind("eth0", NULL), "physical") == 0,
          "без uevent — по имени");
}

/* --- ioctl-путь: префикс из маски и формат cidr (чистые части) --------- */

static void test_mask_prefix(void)
{
    printf("=== префикс из маски сети (ioctl SIOCGIFNETMASK) ===\n");
    /* Маски — уже в хостовом порядке (после ntohl), как их отдаёт iface_ipv4(). */
    CHECK(choice_mask_prefix(0xffffff00UL) == 24, "/24 = 0xffffff00");
    CHECK(choice_mask_prefix(0xffffffffUL) == 32, "/32 = 0xffffffff");
    CHECK(choice_mask_prefix(0xffff0000UL) == 16, "/16 = 0xffff0000");
    CHECK(choice_mask_prefix(0xff000000UL) == 8, "/8 = 0xff000000");
    CHECK(choice_mask_prefix(0xffffff80UL) == 25, "/25 = 0xffffff80");
    CHECK(choice_mask_prefix(0xfffffffcUL) == 30, "/30 = 0xfffffffc");
    CHECK(choice_mask_prefix(0xfffffffeUL) == 31, "/31 = 0xfffffffe");
    CHECK(choice_mask_prefix(0x80000000UL) == 1, "/1 = 0x80000000");
    CHECK(choice_mask_prefix(0UL) == 0, "нулевая маска -> 0");
    CHECK(choice_mask_prefix(0xff00ff00UL) == 8, "неконтигуозная: ведущие единицы");
}

static void test_format_cidr(void)
{
    char b[80];

    printf("=== формат \"адрес/префикс\" для subnets[] ===\n");
    CHECK(choice_format_cidr(b, sizeof(b), 0x0a080002UL, 24) == 1 &&
          strcmp(b, "10.8.0.2/24") == 0, "10.8.0.2/24");
    CHECK(choice_format_cidr(b, sizeof(b), 0xc0a80101UL, 24) == 1 &&
          strcmp(b, "192.168.1.1/24") == 0, "192.168.1.1/24");
    CHECK(choice_format_cidr(b, sizeof(b), 0x7f000001UL, 8) == 1 &&
          strcmp(b, "127.0.0.1/8") == 0, "127.0.0.1/8");
    CHECK(choice_format_cidr(b, sizeof(b), 0xffffffffUL, 32) == 1 &&
          strcmp(b, "255.255.255.255/32") == 0, "255.255.255.255/32");
    CHECK(choice_format_cidr(b, sizeof(b), 0UL, 0) == 1 &&
          strcmp(b, "0.0.0.0/0") == 0, "0.0.0.0/0");
    CHECK(choice_format_cidr(b, sizeof(b), 0x0a080002UL, 33) == 0 && b[0] == '\0',
          "префикс > 32 -> 0 и пустая строка");
    CHECK(choice_format_cidr(b, sizeof(b), 0x0a080002UL, -1) == 0, "отрицательный префикс -> 0");
    CHECK(choice_format_cidr(b, 4, 0x0a080002UL, 24) == 0 && b[0] == '\0',
          "маленький буфер -> 0 (не обрезаем молча)");
    /* Связка целиком: маска от ioctl -> готовый cidr (как в iface_ipv4). */
    CHECK(choice_format_cidr(b, sizeof(b), 0x0a080002UL,
                             choice_mask_prefix(0xffffff00UL)) == 1 &&
          strcmp(b, "10.8.0.2/24") == 0, "маска+адрес -> 10.8.0.2/24");
}

/* --- разбор `ip -4 -o addr show` (теперь лишь ДОПОЛНЕНИЕ к ioctl) ------ */

static void test_parse_dev(void)
{
    char sub[80], dev[64];

    printf("=== разбор строки `ip -4 -o addr show` (доп. источник) ===\n");
    /* Реальный формат busybox/iproute2: имя интерфейса — последним токеном. */
    CHECK(choice_parse_dev("2: eth0    inet 192.168.1.1/24 brd 192.168.1.255 "
                           "scope global eth0\\      valid_lft forever",
                           sub, sizeof(sub), dev, sizeof(dev)) == 1,
          "реальная строка eth0 разобрана (без ключевого слова dev)");
    CHECK(strcmp(sub, "192.168.1.1/24") == 0, "подсеть разобрана");
    CHECK(strcmp(dev, "eth0") == 0, "dev разобран по хвостовому токену");
    /* Формат из ТЗ: "inet A.B.C.D/len ... dev NAME". */
    CHECK(choice_parse_dev("2: eth0    inet 192.168.1.1/24 scope global dev eth0",
                           sub, sizeof(sub), dev, sizeof(dev)) == 1,
          "формат с ключевым словом dev разобран");
    CHECK(strcmp(dev, "eth0") == 0, "dev NAME имеет приоритет");

    /* У loopback нет valid_lft и хвостового «\» — имя даёт только dev NAME. */
    CHECK(choice_parse_dev("1: lo    inet 127.0.0.1/8 scope host dev lo",
                           sub, sizeof(sub), dev, sizeof(dev)) == 1,
           "строка lo разобрана (через dev NAME)");
    CHECK(strcmp(sub, "127.0.0.1/8") == 0 && strcmp(dev, "lo") == 0,
          "lo: подсеть и имя");

    CHECK(choice_parse_dev("3: br-lan    inet 192.168.9.1/24 scope global dev br-lan",
                           sub, sizeof(sub), dev, sizeof(dev)) == 1,
          "имя моста с дефисом разобрано");
    CHECK(strcmp(dev, "br-lan") == 0, "br-lan сохранён целиком");

    CHECK(choice_parse_dev("2: eth0    inet 192.168.1.1 brd ... scope global",
                           sub, sizeof(sub), dev, sizeof(dev)) == 0,
          "без /len — отброшено");
    CHECK(sub[0] == '\0' && dev[0] == '\0', "отброшенная строка не пишет мусор");

    /* Ни «\», ни dev NAME — имя интерфейса не выдумываем. */
    CHECK(choice_parse_dev("2: eth0    inet 10.0.0.1/24 scope global",
                           sub, sizeof(sub), dev, sizeof(dev)) == 0,
          "нет ни «\\», ни dev NAME — отброшено");
    CHECK(choice_parse_dev("2: eth0    inet 10.0.0.1/24 scope global https://x/y",
                           sub, sizeof(sub), dev, sizeof(dev)) == 0,
          "URL не принимается за имя интерфейса");;
    CHECK(choice_parse_dev("", sub, sizeof(sub), dev, sizeof(dev)) == 0,
          "пустая строка — отброшено");
    CHECK(choice_parse_dev(NULL, sub, sizeof(sub), dev, sizeof(dev)) == 0,
          "NULL — отброшено");
    {
        char tiny[4];
        CHECK(choice_parse_dev("2: eth0 inet 192.168.1.1/24 scope global eth0",
                               tiny, sizeof(tiny), dev, sizeof(dev)) == 0,
              "слишком маленький буфер подсети — отброшено");
    }
}

/* --- разбор строки конфига -------------------------------------------- */

static void test_parse_conf(void)
{
    char k[64], v[64];

    printf("=== разбор строки конфига ===\n");
    CHECK(choice_parse_conf_line("profile2_table=202", k, sizeof(k), v, sizeof(v)),
          "profile2_table разобран");
    CHECK(strcmp(k, "profile2_table") == 0 && strcmp(v, "202") == 0,
          "ключ и значение верные");
    CHECK(choice_parse_conf_line("  profile1_mark = 0x08000000  ", k, sizeof(k), v, sizeof(v)),
          "строка с отступами разобрана");
    CHECK(strcmp(k, "profile1_mark") == 0 && strcmp(v, "0x08000000") == 0,
          "отступы обрезаны");
    CHECK(choice_parse_conf_line("  # profile1_table=201", k, sizeof(k), v, sizeof(v)) == 0,
          "комментарий игнорируется");
    CHECK(choice_parse_conf_line("noseparator", k, sizeof(k), v, sizeof(v)) == 0,
          "строка без '=' игнорируется");
    CHECK(choice_parse_conf_line("", k, sizeof(k), v, sizeof(v)) == 0,
          "пустая строка игнорируется");
    CHECK(choice_parse_conf_line("key=", k, sizeof(k), v, sizeof(v)) == 1 && v[0] == '\0',
          "пустое значение допустимо");
}

static void test_looks_like_mark(void)
{
    unsigned long v = 0;

    printf("=== распознавание метки 0x… ===\n");
    CHECK(choice_looks_like_mark("0x40000000", &v) == 1 && v == 0x40000000UL,
          "0x40000000 распознана");
    CHECK(choice_looks_like_mark("0X08000000", &v) == 1 && v == 0x08000000UL,
          "0X08000000 расхождение регистра не мешает");
    CHECK(choice_looks_like_mark("0x40000000 ", &v) == 0, "хвостовой пробел -> не метка");
    CHECK(choice_looks_like_mark("profile1", &v) == 0, "имя профиля не метка");
    CHECK(choice_looks_like_mark("40000000", &v) == 0, "без 0x — не метка");
    CHECK(choice_looks_like_mark("0x", &v) == 0, "0x без цифр — не метка");
    CHECK(choice_looks_like_mark("0", &v) == 0, "односимвольная строка не читается мимо");
    CHECK(choice_looks_like_mark(NULL, &v) == 0, "NULL безопасен");
    CHECK(choice_looks_like_mark("0xzz", &v) == 0, "не-hex — не метка");
}

static void test_mark_range(void)
{
    unsigned long used[4];
    unsigned long first = SUSANIN_MARK_BASE;
    unsigned long all[SUSANIN_MARK_STEP_COUNT];
    int k;

    printf("=== диапазон свободных меток ===\n");
    CHECK(first == 0x40000000UL, "база диапазона 0x40000000");
    CHECK(SUSANIN_MARK_BASE + 15UL * SUSANIN_MARK_STEP == 0x400f0000UL,
          "шаг 0x10000: 16-я метка 0x400f0000");
    CHECK(choice_mark_free_used(NULL, 0) == 0, "без занятых свободна первая");

    used[0] = 0x40000000UL;
    CHECK(choice_mark_used(0x40000000UL, used, 1) == 1, "занятая метка найдена");
    CHECK(choice_mark_used(0x08000000UL, used, 1) == 0, "чужая метка не найдена");
    CHECK(choice_mark_free_used(used, 1) == 1, "вторая свободна, если первая занята");

    for (k = 0; k < SUSANIN_MARK_STEP_COUNT; k++)
        all[k] = SUSANIN_MARK_BASE + (unsigned long)k * SUSANIN_MARK_STEP;
    CHECK(choice_mark_free_used(all, SUSANIN_MARK_STEP_COUNT) == -1,
          "все 16 заняты -> свободных нет");

    used[0] = 0x40000000UL;
    used[1] = 0x40020000UL;
    CHECK(choice_mark_free_used(used, 2) == 1, "дырка находится по порядку");
    CHECK(choice_mark_used(0, used, 2) == 0, "нулевая метка не считается занятой");
}

/* --- сбор из каталогов (host, без /sys) -------------------------------- */

static void test_collect_ifaces(void)
{
    char dir[] = "/tmp/susanin-choices-if-XXXXXX";
    choice_set cs;

    printf("=== collect_ifaces по каталогу ===\n");
    assert(mkdtemp(dir));
    CHECK(mkdir_join(dir, "eth0"), "создан eth0");
    CHECK(mkdir_join(dir, "br0"), "создан br0");
    CHECK(mkdir_join(dir, "nwg0"), "создан nwg0");
    write_file(join_path(dir, "eth0", "operstate"), "up\n");
    write_file(join_path(dir, "br0", "uevent"), "DEVTYPE=bridge\n");
    write_file(join_path(dir, "nwg0", "operstate"), "down\n");

    choice_json_init(&cs);
    collect_ifaces(&cs, dir);
    CHECK(cs.n_ifaces == 3, "найдено три интерфейса");
    CHECK(item_value(&cs, "eth0") && !strcmp(item_value(&cs, "eth0"), "up"),
          "eth0 up");
    CHECK(item_value(&cs, "nwg0") && !strcmp(item_value(&cs, "nwg0"), "down"),
          "nwg0 down");
    CHECK(item_value(&cs, "br0") && !strcmp(item_value(&cs, "br0"), "down"),
          "нет operstate -> down");

    choice_json_init(&cs);
    collect_ifaces(&cs, "/tmp/susanin-no-such-netdir-zz");
    CHECK(cs.n_ifaces == 0, "нет каталога -> пустой массив, без ошибки");
    choice_json_init(&cs);
    collect_ifaces(&cs, NULL);
    CHECK(cs.n_ifaces == 0, "NULL каталог безопасен");
}

static void test_collect_lists(void)
{
    char dir[] = "/tmp/susanin-choices-lists-XXXXXX";
    choice_set cs;

    printf("=== collect_list_files по каталогу ===\n");
    assert(mkdtemp(dir));
    write_file(join_path(dir, "media.txt", NULL), "a\n");
    write_file(join_path(dir, "work.txt", NULL), "b\n");
    write_file(join_path(dir, "notes.md", NULL), "c\n");
    assert(mkdir_join(dir, "sub.txt")); /* каталог с именем *.txt не берём */

    choice_json_init(&cs);
    collect_list_files(&cs, dir);
    CHECK(cs.n_lists == 2, "взяты только обычные файлы *.txt");
    CHECK(item_value(&cs, "media.txt") != NULL, "media.txt в списке");
    CHECK(item_value(&cs, "notes.md") == NULL, "notes.md не в списке");

    choice_json_init(&cs);
    collect_list_files(&cs, "/tmp/susanin-no-such-listdir-zz");
    CHECK(cs.n_lists == 0, "нет каталога -> пустой массив");

    CHECK(!strcmp(wc_list_dir(), SUSANIN_PROFILES_LIST_DIR),
          "подсказка list_dir совпадает с константой");
}

static void test_collect_used(void)
{
    char dir[] = "/tmp/susanin-choices-used-XXXXXX";
    char conf[700];
    susanin_config cfg;
    choice_set cs;

    printf("=== collect_used: таблицы и метки из конфига ===\n");
    assert(mkdtemp(dir));
    snprintf(conf, sizeof(conf), "%s/susanin.conf", dir);
    write_file(conf,
               "# тест\n"
               "profile1_name=media\n"
               "profile1_table=202\n"
               "profile1_mark=0x40000000\n"
               "profile2_table=205\n"        /* профиль без имени (ручная правка) */
               "profile3_mark=0x40010000\n"
               "profile3_list=/opt/susanin/etc/profiles/x.txt\n");

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.profile_name[0], sizeof(cfg.profile_name[0]), "media");
    cfg.profile_table[0] = 202;
    cfg.profile_mark[0] = 0x40000000UL;
    cfg.n_profiles = 1;

    choice_json_init(&cs);
    collect_used(&cs, conf, &cfg);
    CHECK(item_field(cs.tables, cs.n_tables, "202") != NULL, "таблица 202 занята профилем");
    CHECK(item_field(cs.tables, cs.n_tables, "205") != NULL,
          "таблица 205 из сырого конфига тоже занята");
    CHECK(item_field(cs.tables, cs.n_tables, "203") == NULL, "таблица 203 свободна");
    CHECK(item_field(cs.used_marks, cs.n_used_marks, "0x40000000") != NULL,
          "метка профиля 0x40000000 занята");
    CHECK(item_field(cs.used_marks, cs.n_used_marks, "0x40010000") != NULL,
          "метка 0x40010000 из сырого конфига занята");
    CHECK(cs.n_used_marks == 2, "дублей меток нет");

    choice_json_init(&cs);
    collect_used(&cs, "/tmp/susanin-no-such-conf-zz", &cfg);
    CHECK(item_field(cs.tables, cs.n_tables, "202") != NULL,
          "без файла занятость из разобранных профилей сохраняется");
    choice_json_init(&cs);
    collect_used(&cs, NULL, NULL);
    CHECK(cs.n_tables == 0 && cs.n_used_marks == 0, "NULL-аргументы безопасны");
}

static void test_json_end_to_end(void)
{
    char dir[] = "/tmp/susanin-choices-json-XXXXXX";
    char conf[700], net[700], lists[700], out[16384];
    susanin_config cfg;
    size_t n;

    printf("=== wc_write_json: пустой роутер и роутер с данными ===\n");
    assert(mkdtemp(dir));
    snprintf(conf, sizeof(conf), "%s/susanin.conf", dir);
    snprintf(net, sizeof(net), "%s/net", dir);
    snprintf(lists, sizeof(lists), "%s/profiles", dir);
    assert(mkdir_join(dir, "net"));
    assert(mkdir_join(dir, "profiles"));
    write_file(conf, "profile1_name=media\nprofile1_table=201\nprofile1_mark=0x40000000\n"
                     "profile1_list=/opt/susanin/etc/profiles/media.txt\n");
    write_file(join_path(lists, "media.txt", NULL), "one.example.com\n");
    assert(mkdir_join(net, "eth0"));
    assert(mkdir_join(net, "nwg0"));
    write_file(join_path(net, "eth0", "operstate"), "up\n");
    write_file(join_path(net, "nwg0", "operstate"), "up\n");
    write_file(join_path(net, "nwg0", "uevent"), "DEVTYPE=wireguard\n");

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.profile_name[0], sizeof(cfg.profile_name[0]), "media");
    cfg.profile_table[0] = 201;
    cfg.profile_mark[0] = 0x40000000UL;
    cfg.n_profiles = 1;

    n = wc_write_json_src(&cfg, conf, net, lists,
                          WC_SRC_IFACES | WC_SRC_USED | WC_SRC_LISTS,
                          out, sizeof(out));
    CHECK(n > 0 && n == strlen(out), "JSON собран целиком");
    CHECK(strstr(out, "\"ifaces\":[") != NULL, "есть ifaces");
    CHECK(strstr(out, "{\"name\":\"eth0\",\"kind\":\"physical\",\"up\":true}") != NULL,
          "eth0: physical/up");
    CHECK(strstr(out, "{\"name\":\"nwg0\",\"kind\":\"tunnel\",\"up\":true}") != NULL,
          "nwg0: tunnel/up");
    CHECK(strstr(out, "{\"id\":201,\"used\":true}") != NULL, "таблица 201 помечена used");
    CHECK(strstr(out, "{\"id\":202,\"used\":false}") != NULL, "таблица 202 свободна");
    CHECK(strstr(out, "\"used_marks\":[\"0x40000000\"]") != NULL, "used_marks заполнен");
    CHECK(strstr(out, "{\"value\":\"0x40000000\",\"used\":true}") != NULL,
          "первая метка помечена used");
    CHECK(strstr(out, "{\"value\":\"0x40010000\",\"used\":false}") != NULL,
          "вторая метка свободна");
    CHECK(strstr(out, "\"free_mark_index\":1") != NULL, "подсказана следующая свободная метка");
    CHECK(strstr(out, "{\"name\":\"media.txt\",\"path\":") != NULL, "список профиля найден");
    CHECK(strstr(out, "\"list_dir\":\"") != NULL, "list_dir присутствует");
    CHECK(strstr(out, "\"subnets\":[") != NULL, "subnets присутствует (содержимое зависит от `ip`)");

    /* Всё недоступно: пустые массивы, но валидный JSON и код 200. */
    {
        choice_set cs;
        size_t i;
        int depth = 0;

        n = wc_write_json_src(NULL, "/tmp/susanin-no-such-conf-zz",
                              "/tmp/susanin-no-such-net-zz",
                              "/tmp/susanin-no-such-lists-zz",
                              WC_SRC_IFACES | WC_SRC_USED | WC_SRC_LISTS,
                              out, sizeof(out));
        CHECK(n > 0, "без источников JSON всё равно собран");
        CHECK(strstr(out, "\"ifaces\":[]") != NULL, "ifaces пустой");
        CHECK(strstr(out, "\"tables\":[") != NULL, "таблицы перечислены всегда");
        CHECK(strstr(out, "\"used_marks\":[]") != NULL, "used_marks пуст");
        CHECK(strstr(out, "\"lists\":[]") != NULL, "lists пуст");
        CHECK(strstr(out, "\"list_dir\":\"") != NULL, "list_dir присутствует");
        for (i = 0; out[i]; i++) {
            if (out[i] == '{' || out[i] == '[')
                depth++;
            else if (out[i] == '}' || out[i] == ']')
                depth--;
        }
        CHECK(depth == 0, "скобки сбалансированы");
        choice_json_init(&cs);
        CHECK(cs.n_ifaces == 0 && cs.n_subnets == 0, "choice_json_init обнуляет набор");
    }

    /* Буфер меньше ответа — сборщик обязан честно сказать «не влезло». */
    {
        char small[64];
        unsigned src = WC_SRC_IFACES | WC_SRC_USED | WC_SRC_LISTS;
        CHECK(wc_write_json_src(&cfg, conf, net, lists, src, small, sizeof(small)) == 0,
              "маленький буфер -> 0 (вызывающий отдаёт пустой объект)");
        CHECK(wc_write_json_src(&cfg, conf, net, lists, src, out, 0) == 0,
              "нулевой буфер -> 0");
        CHECK(wc_write_json_src(&cfg, conf, net, lists, 0, out, sizeof(out)) > 0,
              "без источников (src=0) JSON всё равно валиден");
    }
}

int main(void)
{
    test_jbuf();
    test_mark_escape();
    test_kinds();
    test_mask_prefix();
    test_format_cidr();
    test_parse_dev();
    test_parse_conf();
    test_looks_like_mark();
    test_mark_range();
    test_collect_ifaces();
    test_collect_lists();
    test_collect_used();
    test_json_end_to_end();

    printf("\n%s\n", failures ? "ЕСТЬ ОШИБКИ" : "все проверки пройдены");
    return failures ? 1 : 0;
}
