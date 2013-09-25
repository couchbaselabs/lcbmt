#include "mt_internal.h"

LIBCOUCHBASE_API
lcbmt_token_t lcb_mt_token_create(lcbmt_t mt)
{
    lcbmt_token_t tok = calloc(1, sizeof(*tok));
    if (!tok) {
        return NULL;
    }
    pthread_mutex_init(&tok->mutex, NULL);
    pthread_cond_init(&tok->cond, NULL);

    tok->parent = mt;
    return tok;
}

LIBCOUCHBASE_API
void lcb_mt_token_destroy(lcbmt_token_t tok)
{
    pthread_mutex_destroy(&tok->mutex);
    pthread_cond_destroy(&tok->cond);
    free(tok);
}

LIBCOUCHBASE_API
void lcb_mt_token_set_cookie(lcbmt_token_t tok, const void *cookie)
{
    tok->ucookie = cookie;
}

LIBCOUCHBASE_API
void lcb_mt_token_set_count(lcbmt_token_t tok, unsigned int count)
{
    tok->remaining = count;
}

typedef void (*generic_callback)(void);

static void dispatch_callback(lcbmt_token_t token)
{
    void (*target)(void) = token->next_callback;
    const struct lcb_mt_callback_table *tbl = &token->parent->callbacks;
    lcb_t instance = token->parent->instance;

    if (target == (generic_callback)tbl->v.v0.store) {
        tbl->v.v0.store(instance,
                        token->ucookie,
                        token->u_cb_special.storop,
                        token->err,
                        (const lcb_store_resp_t *)token->resp);

    } else if (target == (generic_callback)tbl->v.v0.get) {
        tbl->v.v0.get(instance, token->ucookie, token->err,
                      (const lcb_get_resp_t *)token->resp);

    } else if (target == (generic_callback)tbl->v.v0.remove) {
        tbl->v.v0.remove(instance, token->ucookie, token->err,
                         (const lcb_remove_resp_t *)token->resp);

    } else if (target == (generic_callback)tbl->v.v0.arithmetic) {
        tbl->v.v0.arithmetic(instance, token->ucookie, token->err,
                             (const lcb_arithmetic_resp_t*)token->resp);

    } else if (target == (generic_callback)tbl->v.v0.touch) {
        tbl->v.v0.touch(instance, token->ucookie, token->err,
                        (const lcb_touch_resp_t*)token->resp);

    } else if (target == (generic_callback)tbl->v.v0.unlock) {
        tbl->v.v0.unlock(instance, token->ucookie, token->err,
                         (const lcb_unlock_resp_t*)token->resp);

    } else {
        abort();
    }
}

static int get_single_response(lcbmt_token_t token)
{
    int ret;

    pthread_mutex_lock(&token->mutex);
    while (!token->resp) {
        pthread_cond_wait(&token->cond, &token->mutex);
    }

    dispatch_callback(token);
    token->resp = NULL;
    token->next_callback = NULL;
    ret = token->remaining;

    pthread_cond_signal(&token->cond);
    pthread_mutex_unlock(&token->mutex);
    return ret;
}

LIBCOUCHBASE_API
void lcb_mt_token_wait(lcbmt_token_t token)
{
    while (get_single_response(token));
}
