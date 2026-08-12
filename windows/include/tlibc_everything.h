#ifndef TOYC_WINDOWS_TLIBC_EVERYTHING_H
#define TOYC_WINDOWS_TLIBC_EVERYTHING_H

#include "core.h"
#include "ctype.h"
#include "errno.h"
#include "fcntl.h"
#ifndef O_DIRECTORY
#define O_DIRECTORY 0200000
#endif
#include "mman.h"
#include "net.h"
#include "pthread.h"
#include "stdlib.h"
#include "string.h"
#include "time.h"
#include "tlibc_compat.h"
#include "tlibc_print.h"
#include "toy_assets.h"
#include <stdio.h>
long long isqrt(long long value);

#define SIGPIPE 13
int tlibc_sigaction(int signum, void (*handler)(int));
extern char **global_envp;
char *get_env_var(char *envp[], const char *name);

#endif
