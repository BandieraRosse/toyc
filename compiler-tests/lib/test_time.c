/* test_time.c — time.c 功能测试
 * 测试 gmtime_r / mktime / strftime / gettimeofday
 *
 * 注意：2D 数组 bug 已修复（2026-07-23），所有 strftime 格式均可测试。
 * EXPECT: 0
 */

#include "core.h"

extern void __printf(const char *fmt, ...);

static int total, passed;

static void check(const char *name, int cond) {
    total++;
    if (cond) { passed++; __printf("  %s: PASS\n", name); }
    else      { __printf("  %s: FAIL\n", name); }
}

int main(void)
{
    struct tm tm1, tm2;
    time_t t;
    char buf[128];

    __printf("time.c 功能测试\n");
    __printf("---------------\n");

    /* ── gmtime_r(0) = 1970-01-01 00:00:00 UTC (Thu, wday=4) ── */
    t = 0;
    check("gmtime(0) != NULL", gmtime_r(&t, &tm1) != NULL);
    check("epoch: tm_sec  == 0",   tm1.tm_sec   == 0);
    check("epoch: tm_min  == 0",   tm1.tm_min   == 0);
    check("epoch: tm_hour == 0",   tm1.tm_hour  == 0);
    check("epoch: tm_mday == 1",   tm1.tm_mday  == 1);
    check("epoch: tm_mon  == 0",   tm1.tm_mon   == 0);
    check("epoch: tm_year == 70",  tm1.tm_year  == 70);
    check("epoch: tm_wday == 4",   tm1.tm_wday  == 4);
    check("epoch: tm_yday == 0",   tm1.tm_yday  == 0);
    check("epoch: tm_isdst == 0",  tm1.tm_isdst == 0);

    /* ── gmtime_r(946684800) = 2000-01-01 00:00:00 UTC (Sat) ── */
    t = 946684800;
    check("2000 gmtime != NULL", gmtime_r(&t, &tm1) != NULL);
    check("2000: tm_year == 100",  tm1.tm_year == 100);
    check("2000: tm_mon  == 0",    tm1.tm_mon  == 0);
    check("2000: tm_mday == 1",    tm1.tm_mday == 1);
    check("2000: tm_wday == 6",    tm1.tm_wday == 6);
    check("2000: tm_hour == 0",    tm1.tm_hour == 0);
    check("2000: tm_yday == 0",    tm1.tm_yday == 0);

    /* ── localtime_r (currently delegates to gmtime_r) ── */
    t = 0;
    localtime_r(&t, &tm1);
    check("localtime(0): year == 70", tm1.tm_year == 70);

    /* ── mktime: 2000-01-01 00:00:00 ── */
    tm2.tm_year = 100; tm2.tm_mon = 0; tm2.tm_mday = 1;
    tm2.tm_hour = 0; tm2.tm_min = 0; tm2.tm_sec = 0;
    tm2.tm_isdst = -1;
    t = mktime(&tm2);
    check("mktime 2000-01-01", t == 946684800);
    check("mktime backfill tm_wday == 6", tm2.tm_wday == 6);
    check("mktime backfill tm_yday == 0", tm2.tm_yday == 0);

    /* ── mktime → gmtime_r round-trip (post-epoch, mon=0 安全) ── */
    /* 2024-01-15 10:30:45 */
    tm2.tm_year = 124; tm2.tm_mon = 0; tm2.tm_mday = 15;
    tm2.tm_hour = 10; tm2.tm_min = 30; tm2.tm_sec = 45;
    tm2.tm_isdst = -1;
    t = mktime(&tm2);
    gmtime_r(&t, &tm1);
    check("round-trip: tm_year 124",  tm1.tm_year == 124);
    check("round-trip: tm_mon 0",     tm1.tm_mon  == 0);
    check("round-trip: tm_mday 15",   tm1.tm_mday == 15);
    check("round-trip: tm_hour 10",   tm1.tm_hour == 10);
    check("round-trip: tm_min 30",    tm1.tm_min  == 30);
    check("round-trip: tm_sec 45",    tm1.tm_sec  == 45);

    /* ── mktime: 1970-01-01 00:00:00 → 0 ── */
    tm2.tm_year = 70; tm2.tm_mon = 0; tm2.tm_mday = 1;
    tm2.tm_hour = 0; tm2.tm_min = 0; tm2.tm_sec = 0;
    tm2.tm_isdst = -1;
    t = mktime(&tm2);
    check("mktime 1970-01-01 == 0", t == 0);

    /* ── strftime — 数字格式 ── */
    t = 0;
    gmtime_r(&t, &tm1);

    strftime(buf, sizeof(buf), "%Y-%m-%d", &tm1);
    check("strftime %Y-%m-%d",
          buf[0]=='1'&&buf[1]=='9'&&buf[2]=='7'&&buf[3]=='0'
       && buf[5]=='0'&&buf[6]=='1'
       && buf[8]=='0'&&buf[9]=='1');

    strftime(buf, sizeof(buf), "%H:%M:%S", &tm1);
    check("strftime %H:%M:%S",
          buf[0]=='0'&&buf[1]=='0'&&buf[3]=='0'&&buf[4]=='0'
       && buf[6]=='0'&&buf[7]=='0');

    /* 12-hour clock %I */
    tm1.tm_hour = 15; tm1.tm_min = 30; tm1.tm_sec = 0;
    strftime(buf, sizeof(buf), "%I:%M:%S", &tm1);
    check("strftime %I 15:30:00 == 03:30:00",
          buf[0]=='0'&&buf[1]=='3'&&buf[3]=='3'&&buf[4]=='0'
       && buf[6]=='0'&&buf[7]=='0');

    /* Midnight: %I should give 12 */
    tm1.tm_hour = 0; tm1.tm_min = 0; tm1.tm_sec = 0;
    strftime(buf, sizeof(buf), "%I:%M:%S", &tm1);
    check("strftime %I 00:00:00 == 12:00:00",
          buf[0]=='1'&&buf[1]=='2');

    /* %j (day of year) */
    t = 0;
    gmtime_r(&t, &tm1);
    strftime(buf, sizeof(buf), "%j", &tm1);
    check("strftime %j == 001", buf[0]=='0'&&buf[1]=='0'&&buf[2]=='1');

    /* %w (weekday) */
    strftime(buf, sizeof(buf), "%w", &tm1);
    check("strftime %w == 4", buf[0]=='4');

    /* %y (2-digit year) */
    strftime(buf, sizeof(buf), "%y", &tm1);
    check("strftime %y 1970==70", buf[0]=='7'&&buf[1]=='0');

    /* %y for 2000 */
    t = 946684800;
    gmtime_r(&t, &tm1);
    strftime(buf, sizeof(buf), "%y", &tm1);
    check("strftime %y 2000==00", buf[0]=='0'&&buf[1]=='0');

    /* %x (date) */
    strftime(buf, sizeof(buf), "%x", &tm1);
    check("strftime %x starts with 2000",
          buf[0]=='2'&&buf[1]=='0'&&buf[2]=='0'&&buf[3]=='0');

    /* %X (time) */
    strftime(buf, sizeof(buf), "%X", &tm1);
    check("strftime %X == 00:00:00",
          buf[0]=='0'&&buf[1]=='0');

    /* ── strftime — 字符串格式（之前因 2D 数组 bug 禁用，现以修复）── */
    t = 0;
    gmtime_r(&t, &tm1);

    /* %a (weekday abbr): epoch = Thu */
    strftime(buf, sizeof(buf), "%a", &tm1);
    check("strftime %a == Thu",
          buf[0]=='T' && buf[1]=='h' && buf[2]=='u' && buf[3]=='\0');

    /* %A (weekday full): epoch = Thursday */
    strftime(buf, sizeof(buf), "%A", &tm1);
    check("strftime %A == Thursday",
          buf[0]=='T' && buf[1]=='h' && buf[2]=='u' && buf[3]=='r'
       && buf[4]=='s' && buf[5]=='d' && buf[6]=='a' && buf[7]=='y'
       && buf[8]=='\0');

    /* %b (month abbr): epoch = Jan */
    strftime(buf, sizeof(buf), "%b", &tm1);
    check("strftime %b == Jan",
          buf[0]=='J' && buf[1]=='a' && buf[2]=='n' && buf[3]=='\0');

    /* %B (month full): epoch = January */
    strftime(buf, sizeof(buf), "%B", &tm1);
    check("strftime %B == January",
          buf[0]=='J' && buf[1]=='a' && buf[2]=='n' && buf[3]=='u'
       && buf[4]=='a' && buf[5]=='r' && buf[6]=='y' && buf[7]=='\0');

    /* %p (AM/PM): epoch = midnight = AM */
    strftime(buf, sizeof(buf), "%p", &tm1);
    check("strftime %p == AM",
          buf[0]=='A' && buf[1]=='M' && buf[2]=='\0');

    /* %p PM: 13:00 = PM */
    tm1.tm_hour = 13;
    strftime(buf, sizeof(buf), "%p", &tm1);
    check("strftime %p 13:00 == PM",
          buf[0]=='P' && buf[1]=='M' && buf[2]=='\0');

    /* %% literal */
    strftime(buf, sizeof(buf), "%%", &tm1);
    check("strftime %%", buf[0]=='%'&&buf[1]=='\0');

    /* unknown format preserved */
    strftime(buf, sizeof(buf), "%Q", &tm1);
    check("strftime %Q == %Q", buf[0]=='%'&&buf[1]=='Q');

    /* ── gettimeofday ── */
    {
        struct timeval tv;
        int ret = gettimeofday(&tv, NULL);
        check("gettimeofday success", ret == 0);
        check("gettimeofday tv_sec > 1700000000", tv.tv_sec > 1700000000);
    }

    /* ── NULL 处理 ── */
    check("gmtime_r(NULL) == NULL", gmtime_r(NULL, &tm1) == NULL);
    {
        time_t zero = 0;
        check("gmtime_r(NULL result) == NULL", gmtime_r(&zero, NULL) == NULL);
    }
    check("mktime(NULL) == -1", mktime(NULL) == (time_t)-1);

    __printf("---------------\n");
    __printf("结果: %d/%d 通过\n", passed, total);
    return total - passed;
}
