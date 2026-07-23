/* test_procfs.c — procfs.c 功能测试
 * 测试 tlibc_list_pids, tlibc_read_proc_status, tlibc_read_proc_stat,
 * tlibc_jiffies_to_sec, tlibc_format_time
 * EXPECT: 0
 */

#include "core.h"
#include "procfs.h"

extern void __printf(const char *fmt, ...);

static int total, passed;

static void check(const char *name, int cond) {
    total++;
    if (cond) { passed++; __printf("  %s: PASS\n", name); }
    else      { __printf("  %s: FAIL\n", name); }
}

int main(void)
{
    struct proc_status st;
    struct proc_stat  st2;
    char buf[64];
    int ret;

    __printf("procfs.c 功能测试\n");
    __printf("-----------------\n");

    /* ── tlibc_list_pids ── */
    {
        pid_t pids[128];
        int n = tlibc_list_pids(pids, 128);
        __printf("  list_pids() = %d\n", n);
        check("list_pids() > 0 (at least PID 1)", n > 0);
        if (n > 0) {
            check("list_pids()[0] > 0 (first PID valid)", pids[0] > 0);
            int found_init = 0;
            int i;
            for (i = 0; i < n; i++) {
                if (pids[i] == 1) { found_init = 1; break; }
            }
            check("list_pids() contains PID 1", found_init);
        }
    }

    /* ── tlibc_read_proc_status on PID 1 (init) ── */
    ret = tlibc_read_proc_status(1, &st);
    __printf("  read_proc_status(1) = %d\n", ret);
    check("read_proc_status(1) success", ret == 0);
    if (ret == 0) {
        __printf("  pid=%d ppid=%d name='%s' state=%c rss=%ld\n",
                 (int)st.pid, (int)st.ppid, st.name,
                 st.state, st.vm_rss_kb);
        check("status.pid == 1", st.pid == 1);
        check("status.ppid >= 0", st.ppid >= 0);
        check("status.name not empty", st.name[0] != '\0');
        int valid = st.state=='R'||st.state=='S'||st.state=='D'
                 || st.state=='Z'||st.state=='T'||st.state=='X';
        check("status.state valid", valid);
        check("status.vm_rss_kb >= 0", st.vm_rss_kb >= 0);
    }

    /* ── tlibc_read_proc_status on nonexistent PID ── */
    ret = tlibc_read_proc_status(999999999, &st);
    __printf("  read_proc_status(bad) = %d\n", ret);
    check("read_proc_status(bad) == -1", ret == -1);

    /* ── tlibc_read_proc_stat on PID 1 ── */
    ret = tlibc_read_proc_stat(1, &st2);
    __printf("  read_proc_stat(1) = %d\n", ret);
    check("read_proc_stat(1) success", ret == 0);
    if (ret == 0) {
        __printf("  utime=%lu stime=%lu starttime=%lu\n",
                 st2.utime, st2.stime, st2.starttime);
        check("stat starttime > 0", st2.starttime > 0);
    }

    /* ── tlibc_jiffies_to_sec（纯数学） ── */
    check("jiffies_to_sec(0) == 0",   tlibc_jiffies_to_sec(0) == 0);
    check("jiffies_to_sec(100) == 1", tlibc_jiffies_to_sec(100) == 1);
    check("jiffies_to_sec(150) == 1", tlibc_jiffies_to_sec(150) == 1);
    check("jiffies_to_sec(200) == 2", tlibc_jiffies_to_sec(200) == 2);

    /* ── tlibc_format_time（纯数学） ── */
    tlibc_format_time(0, buf, sizeof(buf));
    check("format_time(0) == 0:00",
          buf[0]=='0'&&buf[1]==':'&&buf[2]=='0'&&buf[3]=='0');

    tlibc_format_time(6000, buf, sizeof(buf));
    check("format_time(60s) == 1:00",
          buf[0]=='1'&&buf[1]==':'&&buf[2]=='0'&&buf[3]=='0');

    tlibc_format_time(60000, buf, sizeof(buf));
    check("format_time(600s) == 10:00",
          buf[0]=='1'&&buf[1]=='0'&&buf[2]==':'&&buf[3]=='0'&&buf[4]=='0');

    tlibc_format_time(6100, buf, sizeof(buf));
    check("format_time(61s) == 1:01",
          buf[0]=='1'&&buf[1]==':'&&buf[2]=='0'&&buf[3]=='1');

    __printf("-----------------\n");
    __printf("结果: %d/%d 通过\n", passed, total);
    return total - passed;
}
