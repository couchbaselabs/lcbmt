#include "mt_internal.h"
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

LCBMT_INTERNAL
int lcbmt_init_locks(lcbmt_ctx_t *mt)
{
    int rv;
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setprioceiling(&mattr, 99);

    if ((rv = pthread_mutex_init(&mt->wait_lock, NULL))) {
        return rv;
    }

    if ((rv == pthread_mutex_init(&mt->event_lock, &mattr))) {
        return rv;
    }

    if ((rv == pthread_cond_init(&mt->cond, NULL))) {
        return rv;
    }


    return 0;
}

LCBMT_INTERNAL
void lcbmt_cleanup_locks(lcbmt_ctx_t *mt)
{
    pthread_mutex_destroy(&mt->wait_lock);
    pthread_mutex_destroy(&mt->event_lock);
}

LCBMT_INTERNAL
int lcbmt_blocking_connect(lcbmt_ctx_t *mt)
{
    int rv, old_flags, fd;
    fd = mt->loopsock.ev.fd;
    old_flags = fcntl(fd, F_GETFL);
    if (old_flags == -1) {
        return -1;
    }
    rv = fcntl(fd, F_SETFL, old_flags & ~O_NONBLOCK);
    if (rv == -1) {
        return -1;
    }
    rv = connect(fd, (struct sockaddr *)&mt->saddr, sizeof(mt->saddr));
    if (rv == 0) {
        fcntl(fd, F_SETFL, old_flags);
    }
    return rv;
}

LCBMT_INTERNAL
int lcbmt_set_server_nonblocking(lcbmt_ctx_t *mt)
{
    int rv, fd;
    fd = mt->sock_accepted;
    rv = fcntl(fd, F_GETFL);
    if (rv == -1) {
        return rv;
    }
    rv = fcntl(fd, F_SETFL, rv|O_NONBLOCK);
    return rv;
}

struct mt_info {
    lcbmt_thrfunc fn;
    lcbmt_ctx_t *ctx;
};

static void *pthr_wrap(void *arg)
{
    struct mt_info *info = (struct mt_info *)arg;
    info->fn(info->ctx);
    free(info);
    return NULL;
}


LCBMT_INTERNAL
int lcbmt_start_iops_thread(lcbmt_ctx_t *mt, lcbmt_thrfunc cb)
{
    int rv;
    struct mt_info *info;
    struct sched_param schedp;
    pthread_attr_t tattr;
    pthread_attr_init(&tattr);

    schedp.sched_priority = 99;
    pthread_attr_setschedparam(&tattr, &schedp);

    info = malloc(sizeof(*info));
    info->ctx = mt;
    info->fn = cb;
    rv = pthread_create(&mt->iothread, &tattr, pthr_wrap, info);
    assert(!rv);
    return 0;
}
