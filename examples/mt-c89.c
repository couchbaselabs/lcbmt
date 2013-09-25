#include <libcouchbase/lcbmt.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "cliopts.h"
#include "../src/mt_internal.h"

static int ThreadCount = 4;
static int ValueSize = 48;
static int SecondsRuntime = 0;
static int BatchSize = 1;
static const char *Hostname = "localhost:8091";

static cliopts_entry entries[] = {
    { 't', "threads", CLIOPTS_ARGT_INT, &ThreadCount },
    { 0, "vsize", CLIOPTS_ARGT_INT, &ValueSize },
    { 'T', "time", CLIOPTS_ARGT_INT, &SecondsRuntime },
    { 's', "schedsize", CLIOPTS_ARGT_INT, &BatchSize },
    { 'H', "host", CLIOPTS_ARGT_STRING, &Hostname },
    { 0, NULL }
};

unsigned long global_opcount = 0;
time_t global_begin_time;
typedef struct {
    lcb_t instance;
    lcbmt_t mt;
    lcbmt_token_t token;
    pthread_t thr;
    const lcb_store_cmd_t *store_p;
    const lcb_get_cmd_t *get_p;
    lcb_get_cmd_t gcmd;
    lcb_store_cmd_t scmd;
    int ix;
    char kbuf[4096];
} my_info;

static void *stats_dumper(void *arg)
{
    my_info *info = (my_info *)arg;
    while (1) {
        time_t now = time(NULL);
        float ops_per_sec =
                (float)global_opcount / (float)(now - global_begin_time);

        printf("Ops/Sec: %0.2f, ", ops_per_sec);
        printf("Total: %lu, Invoked: %lu, Notified: %lu, Fast: %lu; QMax: %lu\n",
               global_opcount,
               info->mt->enter_count,
               info->mt->notify_count,
               info->mt->fast_count,
               info->mt->max_queue);

        if (SecondsRuntime &&
                now - global_begin_time > SecondsRuntime) {
            printf("Runtime Exceeded (via commandline)\n");
            exit(0);
        }
        sleep(1);
    }

    return NULL;
}

static void storage_callback(lcb_t instance, const void *cookie,
                             lcb_storage_t op, lcb_error_t err,
                             const lcb_store_resp_t *resp)
{
    my_info *info = (my_info *)cookie;
    assert(err == LCB_SUCCESS);
    assert(resp->version == 0);
    assert(resp->v.v0.nkey == info->scmd.v.v0.nkey);
    global_opcount++;

    /** sanity check to ensure our response data is ok */
}

static void get_callback(lcb_t instance, const void *cookie,
                         lcb_error_t err, const lcb_get_resp_t *resp)
{
    my_info *info = (my_info *)cookie;
    assert(err == LCB_SUCCESS);
    assert(resp->version == 0);
    assert(resp->v.v0.nkey == info->gcmd.v.v0.nkey);
    global_opcount++;
}

static void run_single_op(void *arg)
{
    int ii;
    my_info *info = arg;
    lcb_error_t err;
    lcb_mt_token_set_cookie(info->token, info);
    lcb_mt_token_set_count(info->token, BatchSize);

    /** Lock for scheduling */
    lcb_mt_lock(info->mt);
    for (ii = 0; ii < BatchSize; ii++) {
        err = lcb_store(info->instance, info->token, 1, &info->store_p);
        assert(err == LCB_SUCCESS);
    }
    /** Scheduling done. Unlock */
    lcb_mt_unlock(info->mt);
    /** Equivalent of 'lcb_wait */
    lcb_mt_token_wait(info->token);

    /* Now get a key */
    lcb_mt_lock(info->mt);
    lcb_mt_token_set_count(info->token, BatchSize);
    for (ii = 0; ii < BatchSize; ii++) {
        err = lcb_get(info->instance, info->token, 1, &info->get_p);
        assert(err == LCB_SUCCESS);
    }

    lcb_mt_unlock(info->mt);
    lcb_mt_token_wait(info->token);
}

static void *pthr_run(void *arg)
{
    while (1) {
        run_single_op(arg);
    }
    return NULL;
}

static lcb_t setup_instance(lcb_io_opt_t io)
{
    lcb_t instance;
    struct lcb_create_st cropts = { 0 };
    lcb_error_t err;

    cropts.version = 1;
    cropts.v.v1.io = io;
    cropts.v.v1.host = Hostname;
    err = lcb_create(&instance, &cropts);
    assert(err == LCB_SUCCESS);

    err = lcb_connect(instance);
    assert(err == LCB_SUCCESS);

    err = lcb_wait(instance);
    assert(err == LCB_SUCCESS);

    return instance;
}

int main(int argc, char **argv)
{
    int ii;
    lcbmt_t ctx;
    lcb_t instance;
    lcb_error_t err;
    lcb_io_opt_t io;
    my_info *info_list;
    struct lcb_mt_callback_table cbtable = { 0 };
    char *value;
    pthread_t stats_thr;
    int argpos;

    if (cliopts_parse_options(entries, argc, argv, &argpos, NULL) == -1) {
        exit(1);
    }

    info_list = calloc(ThreadCount, sizeof(*info_list));
    value = calloc(ValueSize, sizeof(*value));

    global_begin_time = time(NULL);
    err = lcb_create_io_ops(&io, NULL);
    assert(err == LCB_SUCCESS);

    instance = setup_instance(io);

    err = lcb_mt_init(&ctx, instance, io);
    assert(err == LCB_SUCCESS);

    cbtable.v.v0.store = storage_callback;
    cbtable.v.v0.get = get_callback;

    lcb_mt_set_callbacks(ctx, &cbtable);

    /** Set up the per-thread information */
    for (ii = 0; ii < ThreadCount; ii++) {
        my_info *info = info_list + ii;
        info->instance = instance;
        info->mt = ctx;
        info->token = lcb_mt_token_create(ctx);
        info->store_p = &info->scmd;
        info->get_p = &info->gcmd;

        sprintf(info->kbuf, "ThrKey:%d\n", ii);
        info->gcmd.v.v0.key = info->kbuf;
        info->gcmd.v.v0.nkey = strlen(info->kbuf);
        info->scmd.v.v0.key = info->gcmd.v.v0.key;
        info->scmd.v.v0.nkey = info->gcmd.v.v0.nkey;
        info->scmd.v.v0.bytes = value;
        info->scmd.v.v0.nbytes = ValueSize;
        info->scmd.v.v0.operation = LCB_SET;

        pthread_create(&info->thr, NULL, pthr_run, info);
    }

    pthread_create(&stats_thr, NULL, stats_dumper, info_list);

    /** Wait until they exit */
    for (ii = 0; ii < ThreadCount; ii++) {
        void *ptr;
        my_info *info = info_list + ii;
        pthread_join(info->thr, &ptr);
        lcb_mt_token_destroy(info->token);
    }

    lcb_destroy(instance);
    lcb_mt_destroy(ctx);
    lcb_destroy_io_ops(io);
    return 0;
}
