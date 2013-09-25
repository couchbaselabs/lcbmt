#ifndef LIBCOUCHBASE_MT_H
#define LIBCOUCHBASE_MT_H
#include <libcouchbase/couchbase.h>
/**
 * This header file contains C extensions for Multi Threaded (MT)
 * libcouchbase usage. Using these files it is possible to make
 * libcouchbase entirely thread safe.
 *
 * Currently the usage of these functions *do* require uses of locking;
 * however the locking is fairly idiomatic.
 *
 * The normal usage of libcouchbase still applies with the following
 * exceptions and caveats:
 *
 * 1) lcb_connect and lcb_wait must initially be called to establish a
 *    successful connection.
 * 2) You must create the IOPS instance itself and maintain it. You can
 *    then pass it to lcb_mt_init()
 * 3) Once lcb_mt_init() is called, lcb_wait must *not* be called
 * 4) It is possible that operation callbacks will be invoked as soon
 *    as lcb_mt_unlock() is called. This is especially likely if the lock
 *    is held for a long time and the network is fast.
 *
 * 5) If you are using the callback interface (e.g. lcb_set_store_callback),
 *    then the callbacks will be invoked in the context of a different
 *    thread (i.e. *not* the calling thread). While future versions of
 *    this library will offer means by which the callbacks will be delivered
 *    to the calling (i.e. scheduling) thread, this must currently be done
 *    from within the application.
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct lcbmt_ctx_st lcbmt_ctx_t, *lcbmt_t;
typedef struct lcbmt_token_st *lcbmt_token_t;

struct lcb_mt_callback_table {
    int version;
    union {
        struct {
            lcb_store_callback store;
            lcb_get_callback get;
            lcb_remove_callback remove;
            lcb_arithmetic_callback arithmetic;
            lcb_touch_callback touch;
            lcb_unlock_callback unlock;
            lcb_stat_callback stats;
            lcb_observe_callback observe;
            lcb_durability_callback endure;
            lcb_http_data_callback http_data;
            lcb_http_complete_callback http_complete;
        } v0;
    } v;
};

/**
 * Initializes a new 'mt' context.
 * @param lcmt_t a pointer to a handle that will refer to the newly
 * created context
 * @param instance the instance to associate with the context
 * @param io the iops structure to be associated. Note that this call
 * should only be invoked after instance creation; IO creation, and after
 * the instance has been connected.
 */
LIBCOUCHBASE_API
lcb_error_t lcb_mt_init(lcbmt_t *mt, lcb_t instance, lcb_io_opt_t io);


/**
 * Lock the context. Once locked, the associated instance (passed to
 * lcb_mt_init) may be safely used by the calling thread. Typically
 * the calling thread will perform one or more scheduling operations
 * (e.g. lcb_store, lcb_get) on the associated instance. Once the scheduling
 * operations have been performed, call lcb_mt_unlock() to release the lock.
 * Once the lock is released, the callback may be invoked.
 *
 * Each call to lcb_mt_lock must be followed by an equivalent call to
 * lcb_mt_unlock. Failure to adhere to this will cause undefined behavior
 * and application hangs due to deadlocks.
 */
LIBCOUCHBASE_API
lcb_error_t lcb_mt_lock(lcbmt_t mt);

/**
 * Unlock the context previously locked by lcb_mt_lock
 */
LIBCOUCHBASE_API
void lcb_mt_unlock(lcbmt_t mt);

/**
 * Signals to the context system that the IO loop is suspended (as it is
 * inside a handler). It is not necessary to use this function, but increased
 * performance may be seen if this is called during handling of a libcouchbase
 * callback.
 *
 * Note that use of this function is required if you wish to perform an
 * operation within the response handler (assuming this is being invoked from
 * within a non-defined thread)
 *
 * Each call to lcb_mt_enter() should be matched with a subsequent call to
 * lcb_mt_leave()
 */
LIBCOUCHBASE_API
void lcb_mt_enter(lcbmt_t mt);

/**
 * Signals to the context system that the control of the calling thread
 * is about to return to the IO loop.
 */
LIBCOUCHBASE_API
void lcb_mt_leave(lcbmt_t mt);

LIBCOUCHBASE_API
void lcb_mt_destroy(lcbmt_t mt);

/**
 * Token API
 * These functions replace the functionality provided by lcb_wait.
 * They ensure that the callbacks are invoked within the context of the
 * calling thread.
 */

/**
 * Creates a new token.
 * @param mt the context.
 *
 * While a token may be reused, there is a 1:1 mapping between the cookie
 * passed to the libcouchbase function and the token created.
 */
LIBCOUCHBASE_API
lcbmt_token_t lcb_mt_token_create(lcbmt_t mt);

/**
 * Sets the cookie for the callback.
 * @param token an initialized token created via token_create()
 * @param cookie a cookie passed as the callback
 */
LIBCOUCHBASE_API
void lcb_mt_token_set_cookie(lcbmt_token_t token, const void *cookie);

/**
 * Set the number of operations scheduled for libcouchbase for this token.
 * This indicates how long token_wait() should block.
 *
 * Internally before each callback, the internal counter is decremented and
 * when it hits 0, 'wait' returns.
 *
 * Note that operations where the count of callbacks is known only later on
 * (e.g. observe, stats), the wrapper will handle it appropriately.
 */
LIBCOUCHBASE_API
void lcb_mt_token_set_count(lcbmt_token_t token, unsigned int count);

/**
 * Set the callbacks for the context. See the structure definition for more
 * details.
 *
 * Note that these callbacks must be set once right after init and before
 * any operations have been scheduled.
 */
LIBCOUCHBASE_API
void lcb_mt_set_callbacks(lcbmt_t mt, const struct lcb_mt_callback_table *tbl);

/**
 * Waits for the callbacks to complete. In this function, the callbacks are
 * invoked from the context of the calling thread. Magic must take place
 */
LIBCOUCHBASE_API
void lcb_mt_token_wait(lcbmt_token_t token);

LIBCOUCHBASE_API
void lcb_mt_token_destroy(lcbmt_token_t token);

#ifdef __cplusplus
}
#endif

#endif
