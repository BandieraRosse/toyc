/* test_thread.c — thread/pthread.c 功能测试
 * 覆盖：pthread_self, pthread_equal, pthread_create, pthread_join
 * 注意：TLS 需要在 main() 开头手动初始化（toyc_rt_start.S 不自动设置 fs_base）
 * EXPECT: 0
 */

#include "core.h"
#include "pthread.h"
#include "syscall.h"
#include "syscall_num.h"

extern void __printf(const char *fmt, ...);

static int total, passed;

static void check(const char *name, int cond) {
    total++;
    if (cond) { passed++; __printf("  %s: PASS\n", name); }
    else      { __printf("  %s: FAIL\n", name); }
}

/* ── 主线程 TLS 初始化 ─────────────────────────────────────────── */

static struct pthread __main_thread_tls;

static void setup_main_thread_tls(void) {
    __memset(&__main_thread_tls, 0, sizeof(struct pthread));
    __main_thread_tls.self        = &__main_thread_tls;
    __main_thread_tls.dtv         = NULL;
    __main_thread_tls.canary      = 0;
    __main_thread_tls.tid         = __gettid();
    __main_thread_tls.errno_val   = 0;
    __main_thread_tls.detach_state = PTHREAD_CREATE_JOINABLE;
    __main_thread_tls.map_base    = NULL;
    __main_thread_tls.map_size    = 0;
    __main_thread_tls.result      = NULL;

    long ret = syscall(SYS_arch_prctl, 0x1002 /* ARCH_SET_FS */,
                       (unsigned long)&__main_thread_tls);
    if (ret != 0)
        __printf("Warning: arch_prctl(ARCH_SET_FS) failed, ret=%ld\n", ret);
}

/* ── 子线程函数 ─────────────────────────────────────────────────── */

struct thread_arg {
    int input;
    int output;
};

static void *worker(void *arg) {
    struct thread_arg *ta = (struct thread_arg *)arg;
    ta->output = ta->input * 2;
    return (void *)(long)(ta->input + 100);
}

/* ── 测试：pthread_equal ────────────────────────────────────────── */

static int test_equal_different(void) {
    /* 模拟两个不同的 pthread_t（实际测试指针比较） */
    struct pthread a, b;
    __memset(&a, 0, sizeof(a));
    __memset(&b, 0, sizeof(b));
    return !pthread_equal(&a, &b);  /* 应不相等 */
}

/* ── 主测试流程 ─────────────────────────────────────────────────── */

int main(void)
{
    setup_main_thread_tls();

    __printf("pthread.c 功能测试\n");
    __printf("-----------------\n");

    /* 1. pthread_self */
    {
        pthread_t self = pthread_self();
        check("pthread_self non-NULL", self != NULL);
        check("pthread_self == main_tls", self == &__main_thread_tls);
    }

    /* 2. pthread_equal */
    {
        pthread_t self = pthread_self();
        check("pthread_equal(self, self)", pthread_equal(self, self));
        check("pthread_equal(self, &__main_thread_tls)",
              pthread_equal(self, &__main_thread_tls));
        check("pthread_equal different", test_equal_different());
    }

    /* 3. pthread_create + pthread_join */
    {
        struct thread_arg ta;
        ta.input = 42;
        ta.output = 0;
        pthread_t t;

        int rc = pthread_create(&t, NULL, worker, &ta);
        check("pthread_create success", rc == 0);
        check("pthread_t non-NULL", t != NULL);

        if (rc == 0) {
            void *retval;
            rc = pthread_join(t, &retval);
            check("pthread_join success", rc == 0);
            check("worker output field correct", ta.output == 84);
            check("worker return value correct",
                  (long)retval == (long)(42 + 100));
        }
    }

    /* 4. pthread_create with different arg values */
    {
        pthread_t t1, t2;
        struct thread_arg ta1 = {7, 0};
        struct thread_arg ta2 = {99, 0};

        int rc1 = pthread_create(&t1, NULL, worker, &ta1);
        int rc2 = pthread_create(&t2, NULL, worker, &ta2);
        check("pthread_create t1", rc1 == 0);
        check("pthread_create t2", rc2 == 0);

        if (rc1 == 0 && rc2 == 0) {
            void *r1, *r2;
            pthread_join(t1, &r1);
            pthread_join(t2, &r2);
            check("worker t1 output=14", ta1.output == 14);
            check("worker t2 output=198", ta2.output == 198);
            check("worker t1 retval=107", (long)r1 == 107);
            check("worker t2 retval=199", (long)r2 == 199);
        }
    }

    /* ── 汇总 ── */
    __printf("-----------------\n");
    __printf("结果: %d/%d 通过\n", passed, total);
    return total - passed;
}
