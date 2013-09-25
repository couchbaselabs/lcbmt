#define LIBCOUCHBASE_INTERNAL
#ifndef LCB_MT_INTERNAL_H
#define LCB_MT_INTERNAL_H
#include <libcouchbase/lcbmt.h>

#include "platform_internal.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LCBMT_INTERNAL

typedef void (*lcbmt_thrfunc)(lcbmt_ctx_t *);


LCBMT_INTERNAL
int lcbmt_init_locks(lcbmt_ctx_t *);

LCBMT_INTERNAL
void lcbmt_cleanup_locks(lcbmt_ctx_t *);


typedef enum {
    /**
     * Mutex to lock the actual event. This is held by the IO when active,
     * and released before it is about to return
     */
    LCBMT_LOCK_EVENT = 0,

    /**
     * Lock to be acquired when requesting a wait sequence. This happens on
     * the client side.
     */
    LCBMT_LOCK_WAITREQUEST,

    /**
     * Lock the wait lock to ensure nothing else is waiting
     */
    LCBMT_LOCK_WAITCLEAR
} lcbmt_lock_target;

LCBMT_INTERNAL
void lcbmt_wait_lock(lcbmt_ctx_t *proxy, lcbmt_lock_target target);

LCBMT_INTERNAL
void lcbmt_release_lock(lcbmt_ctx_t *proxy, lcbmt_lock_target target);

LCBMT_INTERNAL
int lcbmt_start_iops_thread(lcbmt_ctx_t *, lcbmt_thrfunc);

LCBMT_INTERNAL
int lcbmt_blocking_connect(lcbmt_ctx_t *);

LCBMT_INTERNAL
int lcbmt_set_server_nonblocking(lcbmt_ctx_t *mt);

int lcbmt_notify(lcbmt_ctx_t *proxy);

LCBMT_INTERNAL
void lcbmt_wrap_callbacks(lcbmt_ctx_t *mt, lcb_t instance);

/**
 * Sets up the listening socket. The listening socket is established
 * from outside the IO thread (i.e. it is established from the calling thread);
 * because the connection from the IOPS thread must be made by one of the
 * internal connect routines.
 */
int lcbmt_setup_socket(lcbmt_ctx_t *);

/**
 * Call this from the IOPS thread. Waits until the socket is connected
 */
int lcbmt_negotiate_client(lcbmt_ctx_t *proxy);

/**
 * Call this from the main thread. Waits until accept returns with the
 * incoming connection made from the IOPS thread
 */
int lcbmt_negotiate_server(lcbmt_ctx_t *proxy);

/**
 * This is called from the IO routines.
 */
void lcbmt_internal_callback(lcbmt_ctx_t *);

typedef struct lcbmt_ctx_st {
    LCBMT_CTX_FIELDS

    lcb_socket_t sock_lsn;
    lcb_socket_t sock_accepted;
    union {
        struct {
            lcb_sockdata_t *sd;
            char *dummy_root;
            char *dummy_rb;
            char buf[4096];
        } iocp;

        struct {
            void *event;
            lcb_socket_t fd;
        } ev;
    } loopsock;

    /** Whether we're entered into the loop */
    int entered;

    /** Whether a byte has been sent to the socket */
    int signalled;

    /** How many waiters */
    unsigned int volatile waiters;

    struct sockaddr_in saddr;
    struct lcb_io_opt_st *iops;
    lcb_t instance;

    /** Callbacks */
    struct lcb_mt_callback_table callbacks;

    /** Statistics */
    unsigned long notify_count;
    unsigned long enter_count;
    unsigned long fast_count;
    unsigned long max_queue;

} lcbmt_ctx_t;

typedef struct lcbmt_token_st {
    LCBMT_TOKEN_FIELDS


    /** Actual cookie passed */
    const void *ucookie;
    lcbmt_ctx_t *parent;

    /** "Out" fields. These are reset in each callback */
    unsigned int remaining;
    lcb_error_t err;
    const void *resp;
    void (*next_callback)(void);

    /** Special arguments for individual callbacks */
    union {
        lcb_storage_t storop;
        lcb_http_request_t htreq;
    } u_cb_special;

} *lcbmt_token_t;

#endif
