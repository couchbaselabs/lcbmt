#ifndef LCBMT_UNIX_H
#define LCBMT_UNIX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#define closesocket close

#define LCBMT_CTX_FIELDS \
    pthread_t iothread; \
    pthread_mutex_t wait_lock; \
    pthread_mutex_t event_lock; \
    pthread_cond_t cond;

#define LCBMT_TOKEN_FIELDS \
    pthread_mutex_t mutex; \
    pthread_cond_t cond; \


#ifdef __cplusplus
}
#endif /* cplusplus */

#endif /*LCBMT_UNIX_H */
