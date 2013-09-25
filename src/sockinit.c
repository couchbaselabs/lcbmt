#include "mt_internal.h"
#include <stdlib.h>
#include <stdio.h>

/**
 * These files just contain the boilerplate routines to set up
 * the listening and accepting sockets for the IO proxy system.
 *
 * The actual locking code may be found in plugin.c
 */


static int mt_reschedule_read(lcbmt_ctx_t *mt);

int lcbmt_setup_socket(lcbmt_ctx_t *mt)
{
    int rv;
    socklen_t slen;

    mt->sock_lsn = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (mt->sock_lsn == -1) {
        return -1;
    }

    rv = bind(mt->sock_lsn,
              (struct sockaddr *)&mt->saddr,
              sizeof(mt->saddr));

    if (rv != 0) {
        return -1;
    }

    slen = sizeof(mt->sock_lsn);
    rv = getsockname(mt->sock_lsn,
                     (struct sockaddr*)&mt->saddr,
                     &slen);

    if (rv != 0) {
        return -1;
    }

    rv = listen(mt->sock_lsn, 5);
    if (rv != 0) {
        return -1;
    }

    return 0;
}

static void connect_callback(lcb_sockdata_t *sock, int status)
{
    int *status_p;
    status_p = (int *)sock->lcbconn;
    *status_p = status;
    sock->parent->v.v1.stop_event_loop(sock->parent);
}

static void reset_v1_buf(lcbmt_ctx_t *mt)
{
    mt->loopsock.iocp.sd->read_buffer.iov[0].iov_base =
            mt->loopsock.iocp.buf;
    mt->loopsock.iocp.sd->read_buffer.iov[0].iov_len =
            sizeof(mt->loopsock.iocp.buf);
}

int lcbmt_negotiate_client(lcbmt_ctx_t *mt)
{
    int rv;
    lcb_io_opt_t io = mt->iops;
    if (mt->iops->version == 0) {
        int optval = 1;
        struct lcb_iops_table_v0_st *v0 = &io->v.v0;
        mt->loopsock.ev.event = v0->create_event(io);
        if (!mt->loopsock.ev.event) {
            return -1;
        }
        mt->loopsock.ev.fd = v0->socket(io, AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (mt->loopsock.ev.fd == -1) {
            return -1;
        }
        rv = lcbmt_blocking_connect(mt);
        setsockopt(mt->loopsock.ev.fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

     } else {
         struct lcb_iops_table_v1_st *v1 = &io->v.v1;
         mt->loopsock.iocp.sd = v1->create_socket(io,
                                                  AF_INET, SOCK_STREAM,
                                                  IPPROTO_TCP);
         if (!mt->loopsock.iocp.sd) {
             return -1;
         }

         mt->loopsock.iocp.sd->lcbconn = (struct lcb_connection_st *)&rv;
         mt->loopsock.iocp.sd->read_buffer.root = malloc(1);
         mt->loopsock.iocp.sd->read_buffer.ringbuffer = malloc(1);
         reset_v1_buf(mt);

         rv = v1->start_connect(io, mt->loopsock.iocp.sd,
                                (struct sockaddr *)&mt->saddr,
                                sizeof(mt->saddr),
                                connect_callback);
         v1->run_event_loop(io);
    }
    if (rv == 0) {
        mt_reschedule_read(mt);
    }
    return rv;
}

int lcbmt_negotiate_server(lcbmt_ctx_t *mt)
{
    struct sockaddr_storage caddr;
    socklen_t slen = sizeof(caddr);
    int optval = 1;

    mt->sock_accepted = accept(mt->sock_lsn, (struct sockaddr *)&caddr, &slen);

    if (mt->sock_accepted == -1) {
        return -1;
    }
    setsockopt(mt->sock_accepted, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
    return lcbmt_set_server_nonblocking(mt);
}


static void mt_v0_callback(lcb_socket_t sock, short which, void *arg)
{
    char buf[4096];
    ssize_t rv;
    lcbmt_ctx_t *mt = arg;
    while ( (rv = recv(mt->loopsock.ev.fd, buf,
                       sizeof(buf), 0)) == sizeof(buf)) {
        /* no body */
    }
    if (rv == 0) {
        fprintf(stderr, "Connection closed!\n");
    }

    lcbmt_internal_callback(mt);
    mt_reschedule_read(mt);
}

static void mt_v1_callback(lcb_sockdata_t* sock, lcb_ssize_t nr)
{
    lcbmt_ctx_t *mt = (lcbmt_ctx_t *)sock->lcbconn;
    lcbmt_internal_callback(mt);
    mt_reschedule_read(mt);
}

static int mt_reschedule_read(lcbmt_ctx_t *mt)
{
    if (mt->iops->version == 0) {
        mt->iops->v.v0.update_event(mt->iops,
                                    mt->loopsock.ev.fd,
                                    mt->loopsock.ev.event,
                                    LCB_READ_EVENT,
                                    mt,
                                    mt_v0_callback);
    } else {
        reset_v1_buf(mt);
        mt->iops->v.v1.start_read(mt->iops,
                                  mt->loopsock.iocp.sd,
                                  mt_v1_callback);
    }
    return 0;
}

int lcbmt_notify(lcbmt_ctx_t *mt)
{
    char c = '*';
    send(mt->sock_accepted, &c, sizeof(c), MSG_DONTWAIT);
    mt->notify_count++;
    return 0;
}
