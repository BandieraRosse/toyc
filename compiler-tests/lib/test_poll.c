/* test_poll.c — poll.c 功能测试
 * 测试 poll / epoll 通过 pipe 进行 I/O 多路复用
 * EXPECT: 0
 */

#include "core.h"
#include "posix/poll.h"
#include "posix/sys/epoll.h"

extern void __printf(const char *fmt, ...);

static int total, passed;

static void check(const char *name, int cond) {
    total++;
    if (cond) { passed++; __printf("  %s: PASS\n", name); }
    else      { __printf("  %s: FAIL\n", name); }
}

int main(void)
{
    int fds[2];
    int ret, epfd, n;
    char buf[16];

    __printf("poll.c 功能测试\n");
    __printf("---------------\n");

    /* ── pipe 创建 ── */
    ret = __pipe2(fds, 0);
    check("pipe2 success", ret == 0);

    /* ── poll: 空 pipe 超时 10ms，应返回 0（无事件） ── */
    {
        struct pollfd pfd;
        pfd.fd = fds[0];     /* 读端 */
        pfd.events = POLLIN;
        pfd.revents = 0;
        ret = poll(&pfd, 1, 10);  /* 10ms 超时 */
        check("poll empty: timeout = 0", ret == 0);
        check("poll empty: revents == 0", pfd.revents == 0);
    }

    /* ── poll: 写入后应可读 ── */
    {
        struct pollfd pfd;
        n = __write(fds[1], "x", 1);
        check("write to pipe", n == 1);

        pfd.fd = fds[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
        ret = poll(&pfd, 1, 100);
        check("poll after write: ret > 0", ret > 0);
        check("poll after write: POLLIN set", (pfd.revents & POLLIN) != 0);
    }

    /* ── poll: POLLOUT always set for pipe write end ── */
    {
        struct pollfd pfd;
        pfd.fd = fds[1];
        pfd.events = POLLOUT;
        pfd.revents = 0;
        ret = poll(&pfd, 1, 10);
        check("poll POLLOUT: ret > 0", ret > 0);
        check("poll POLLOUT: POLLOUT set", (pfd.revents & POLLOUT) != 0);
    }

    /* ── poll: invalid fd ── */
    {
        struct pollfd pfd;
        pfd.fd = 9999;
        pfd.events = POLLIN;
        pfd.revents = 0;
        ret = poll(&pfd, 1, 0);
        check("poll bad fd: revents POLLNVAL", (pfd.revents & POLLNVAL) != 0);
    }

    /* ── epoll_create1 ── */
    epfd = epoll_create1(0);
    check("epoll_create1 success", epfd >= 0);

    /* ── epoll_ctl: add pipe read end ── */
    {
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = fds[0];
        ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fds[0], &ev);
        check("epoll_ctl ADD success", ret == 0);
    }

    /* ── epoll_wait: should be ready (data already in pipe) ── */
    {
        struct epoll_event events[2];
        ret = epoll_wait(epfd, events, 2, 10);
        check("epoll_wait after write: ret > 0", ret > 0);
        check("epoll_wait: fd match", ret > 0 && events[0].data.fd == fds[0]);
        check("epoll_wait: EPOLLIN set",
              ret > 0 && (events[0].events & EPOLLIN) != 0);
    }

    /* ── epoll_ctl: del ── */
    ret = epoll_ctl(epfd, EPOLL_CTL_DEL, fds[0], NULL);
    check("epoll_ctl DEL success", ret == 0);

    /* ── epoll_wait after del: timeout ── */
    {
        struct epoll_event events[2];
        ret = epoll_wait(epfd, events, 2, 10);
        check("epoll_wait after del: timeout == 0", ret == 0);
    }

    /* ── epoll_create1 with EPOLL_CLOEXEC ── */
    {
        int efd = epoll_create1(EPOLL_CLOEXEC);
        check("epoll_create1(CLOEXEC) success", efd >= 0);
        if (efd >= 0) __close(efd);
    }

    /* ── 读取 pipe 中剩余数据后关闭 ── */
    n = __read(fds[0], buf, sizeof(buf));
    __close(fds[0]);
    __close(fds[1]);
    if (epfd >= 0) __close(epfd);

    __printf("---------------\n");
    __printf("结果: %d/%d 通过\n", passed, total);
    return total - passed;
}
