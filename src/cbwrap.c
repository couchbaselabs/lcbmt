#include "mt_internal.h"

#define SET_NEXT_CALLBACK(tok, name) \
    tok->next_callback = (void(*)(void))tok->parent->callbacks.v.v0.name

#define ASSIGN_COMMON(tok, err, resp) \
    tok->err = err; \
    tok->resp = resp;

static void token_enter(lcbmt_token_t token)
{
    pthread_mutex_lock(&token->mutex);
}

static void token_leave(lcbmt_token_t token, unsigned int decrcount)
{
    /** This mutex should be unlocked by the cond_wait loop in token.c */
    lcb_mt_enter(token->parent);

    token->remaining -= decrcount;
    assert(token->next_callback);

    /**
     * Signal that we're done setting information in the token. We don't
     * need to lock here since it's already done in token_enter()
     */
    pthread_cond_signal(&token->cond);

    /**
     * This implies an unlock.
     */
    while (token->next_callback) {
        pthread_cond_wait(&token->cond, &token->mutex);
    }
    pthread_mutex_unlock(&token->mutex);
    lcb_mt_leave(token->parent);
}

static void store_callback(lcb_t instance,
                           const void *cookie,
                           lcb_storage_t op,
                           lcb_error_t err,
                           const lcb_store_resp_t *resp)
{
    lcbmt_token_t token = (lcbmt_token_t)cookie;
    token_enter(token);
    SET_NEXT_CALLBACK(token, store);
    ASSIGN_COMMON(token, err, resp);
    token_leave(token, 1);
}


static void stats_callback(lcb_t instance, const void *cookie,
                           lcb_error_t err,
                           const lcb_server_stat_resp_t *resp)
{
    lcbmt_token_t token = (lcbmt_token_t)cookie;
    int decrcount = 0;

    token_enter(token);
    SET_NEXT_CALLBACK(token, stats);
    ASSIGN_COMMON(token, err, resp);

    if (resp->v.v0.server_endpoint == NULL) {
        decrcount = 1;
    }
    token_leave(token, decrcount);
}

static void http_data_callback(lcb_http_request_t htreq,
                               lcb_t instance,
                               const void *cookie,
                               lcb_error_t err,
                               const lcb_http_resp_t *resp)
{
    lcbmt_token_t token = (lcbmt_token_t)cookie;

    token_enter(token);

    SET_NEXT_CALLBACK(token, http_data);
    ASSIGN_COMMON(token, err, resp);
    token->u_cb_special.htreq = htreq;

    token_leave(token, 0);
}

static void http_complete_callback(lcb_http_request_t htreq,
                                   lcb_t instance,
                                   const void *cookie,
                                   lcb_error_t err,
                                   const lcb_http_resp_t *resp)
{
    lcbmt_token_t token = (lcbmt_token_t)cookie;
    token_enter(token);

    SET_NEXT_CALLBACK(token, http_complete);
    ASSIGN_COMMON(token, err, resp);
    token->u_cb_special.htreq = htreq;

    token_leave(token, 1);
}


#define DECLARE_CALLBACK(t_resp, cb_fld, name) \
static void name(lcb_t instance, const void *cookie, lcb_error_t err, \
                 const t_resp *resp) \
{ \
    lcbmt_token_t token = (lcbmt_token_t)cookie; \
    token_enter(token); \
    SET_NEXT_CALLBACK(token, cb_fld); \
    token->resp = resp; \
    token->err = err; \
    token_leave(token, 1); \
}

DECLARE_CALLBACK(lcb_get_resp_t, get, get_callback)
DECLARE_CALLBACK(lcb_remove_resp_t, remove, remove_callback)
DECLARE_CALLBACK(lcb_arithmetic_resp_t, arithmetic, arithmetic_callback)
DECLARE_CALLBACK(lcb_touch_resp_t, touch, touch_callback)
DECLARE_CALLBACK(lcb_unlock_resp_t, unlock, unlock_callback)
DECLARE_CALLBACK(lcb_durability_resp_t, endure, endure_callback);

LCBMT_INTERNAL
void lcbmt_wrap_callbacks(lcbmt_ctx_t *mt, lcb_t instance)
{
    lcb_set_store_callback(instance, store_callback);
    lcb_set_get_callback(instance, get_callback);
    lcb_set_remove_callback(instance, remove_callback);
    lcb_set_arithmetic_callback(instance, arithmetic_callback);
    lcb_set_touch_callback(instance, touch_callback);
    lcb_set_unlock_callback(instance, unlock_callback);
    lcb_set_stat_callback(instance, stats_callback);
    lcb_set_durability_callback(instance, endure_callback);
    lcb_set_http_data_callback(instance, http_data_callback);
    lcb_set_http_complete_callback(instance, http_complete_callback);
}

LIBCOUCHBASE_API
void lcb_mt_set_callbacks(lcbmt_t mtp, const struct lcb_mt_callback_table *tbl)
{
    mtp->callbacks = *tbl;
}
