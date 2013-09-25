#include "mt_internal.h"
#include <stdlib.h>
#include <stdio.h>
/**
 * Run from within the IOPS thread.
 */
static void lcbmt_internal_run(lcbmt_ctx_t *mt)
{
    if (lcbmt_negotiate_client(mt) != 0) {
        fprintf(stderr, "Couldn't negotiate client connection..\n");
        return;
    }

    while (1) {
        pthread_mutex_lock(&mt->event_lock);
        pthread_cond_wait(&mt->cond, &mt->event_lock);
        lcb_wait(mt->instance);
        pthread_mutex_unlock(&mt->event_lock);
    }
}

/**
 * Handler invoked by the socket callback (from the event loop)
 */
void lcbmt_internal_callback(lcbmt_ctx_t *mt)
{
    mt->enter_count++;
    lcb_mt_enter(mt);
    lcb_mt_leave(mt);
}

static void set_wait_start(lcbmt_ctx_t *mt)
{
    pthread_mutex_lock(&mt->wait_lock);
    mt->waiters++;
}

static void set_wait_done(lcbmt_ctx_t *mt)
{
    pthread_mutex_unlock(&mt->wait_lock);
}

static void wait_for_schedulers(lcbmt_ctx_t *mt)
{
    pthread_mutex_lock(&mt->wait_lock);
    if (mt->waiters > mt->max_queue) {
        mt->max_queue = mt->waiters;
    }
    mt->waiters = 0;

    pthread_mutex_lock(&mt->event_lock);
    pthread_mutex_unlock(&mt->wait_lock);
}

LIBCOUCHBASE_API
void lcb_mt_enter(lcbmt_ctx_t *mt)
{
    /**
     * Set the fact that we're entered. This tells threads waiting on the
     * event mutex to not send spurious I/O events; and that any lock
     * contention is due to a different scheduler thread using it.
     */
    pthread_mutex_unlock(&mt->event_lock);
}

LIBCOUCHBASE_API
void lcb_mt_leave(lcbmt_ctx_t *mt)
{
    /**
     * Before we lock the event mutex back, we must make sure that there's
     * no thread which is about to acquire the event mutex. This can happen
     * because we reach the parent function typically when a socket event
     * occurs. The socket event itself may take place *before* the scheuler
     * has had the chance to enter the lock queue.
     */
    wait_for_schedulers(mt);
}

/**
 * Basic pattern:
 * Try to get a lock immediately. If that doesn't work,
 * see if another thread has sent a socket request yet (so we don't spam
 * the socket).
 */
LIBCOUCHBASE_API
lcb_error_t lcb_mt_lock(lcbmt_t mtp)
{
    if (pthread_mutex_trylock(&mtp->event_lock)) {
        set_wait_start(mtp);
        lcbmt_notify(mtp);
        pthread_mutex_lock(&mtp->event_lock);
        set_wait_done(mtp);

    } else {
        mtp->fast_count++;
    }

    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
void lcb_mt_unlock(lcbmt_t mtp)
{
    pthread_cond_signal(&mtp->cond);
    pthread_mutex_unlock(&mtp->event_lock);
}

/**
 * Call this after the instance has been set, and the connection to
 * us has been initiated
 */
lcb_error_t lcb_mt_io_start(lcbmt_t mtp)
{
    if (lcbmt_start_iops_thread(mtp, lcbmt_internal_run) != 0) {
        return LCB_EINTERNAL;
    }

    if (lcbmt_negotiate_server(mtp) != 0) {
        return LCB_EINTERNAL;
    }

    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
void lcb_mt_destroy(lcbmt_t mtp)
{
    lcbmt_cleanup_locks(mtp);

    if (mtp->sock_lsn != -1) {
        closesocket(mtp->sock_lsn);
    }

    if (mtp->sock_accepted != -1) {
        closesocket(mtp->sock_accepted);
    }

    free(mtp);
}

LIBCOUCHBASE_API
lcb_error_t lcb_mt_init(lcbmt_t *mtpp, lcb_t instance, lcb_io_opt_t io)
{
    *mtpp = calloc(1, sizeof(**mtpp));
    if (!*mtpp) {
        return LCB_CLIENT_ENOMEM;
    }

    if (lcbmt_init_locks(*mtpp) != 0) {
        lcb_mt_destroy(*mtpp);
        return LCB_EINTERNAL;
    }

    if (lcbmt_setup_socket(*mtpp) != 0) {
        lcb_mt_destroy(*mtpp);
        return LCB_EINTERNAL;
    }

    (*mtpp)->iops = io;
    (*mtpp)->instance = instance;

    if (lcb_mt_io_start(*mtpp) != 0) {
        lcb_mt_destroy(*mtpp);
        return LCB_EINTERNAL;
    }

    lcbmt_wrap_callbacks(*mtpp, instance);


    return LCB_SUCCESS;
}
