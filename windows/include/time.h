#ifndef TOYC_WINDOWS_TIME_H
#define TOYC_WINDOWS_TIME_H

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_THREAD_CPUTIME_ID 3
#define TIMER_ABSTIME 1

typedef int clockid_t;

#ifndef _TIMESPEC_DEFINED
#define _TIMESPEC_DEFINED
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#endif

#endif
