/*
 * Copyright (C) 2010      Citrix Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 only. with the special
 * exception on linking described in file LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 */

#include "libxl_osdeps.h" /* must come before any other headers */

#include "libxl_internal.h"

/*----- data copier -----*/

void libxl__datacopier_init(libxl__datacopier_state *dc)
{
    assert(dc->ao);
    libxl__ev_fd_init(&dc->toread);
    libxl__ev_fd_init(&dc->towrite);
    LIBXL_TAILQ_INIT(&dc->bufs);
}

void libxl__datacopier_kill(libxl__datacopier_state *dc)
{
    STATE_AO_GC(dc->ao);
    libxl__datacopier_buf *buf, *tbuf;

    libxl__ev_fd_deregister(gc, &dc->toread);
    libxl__ev_fd_deregister(gc, &dc->towrite);
    LIBXL_TAILQ_FOREACH_SAFE(buf, &dc->bufs, entry, tbuf)
        free(buf);
    LIBXL_TAILQ_INIT(&dc->bufs);
}

static void datacopier_callback(libxl__egc *egc, libxl__datacopier_state *dc,
                                int onwrite, int errnoval)
{
    libxl__datacopier_kill(dc);
    dc->callback(egc, dc, onwrite, errnoval);
}

static void datacopier_writable(libxl__egc *egc, libxl__ev_fd *ev,
                                int fd, short events, short revents);

static void datacopier_check_state(libxl__egc *egc, libxl__datacopier_state *dc)
{
    STATE_AO_GC(dc->ao);
    int rc;
    
    if (dc->used) {
        if (!libxl__ev_fd_isregistered(&dc->towrite)) {
            rc = libxl__ev_fd_register(gc, &dc->towrite, datacopier_writable,
                                       dc->writefd, POLLOUT);
            if (rc) {
                LOG(ERROR, "unable to establish write event on %s"
                    " during copy of %s", dc->writewhat, dc->copywhat);
                datacopier_callback(egc, dc, -1, 0);
                return;
            }
        }
    } else if (!libxl__ev_fd_isregistered(&dc->toread)) {
        /* we have had eof */
        datacopier_callback(egc, dc, 0, 0);
        return;
    } else {
        /* nothing buffered, but still reading */
        libxl__ev_fd_deregister(gc, &dc->towrite);
    }
}

void libxl__datacopier_prefixdata(libxl__egc *egc, libxl__datacopier_state *dc,
                                  const void *data, size_t len)
{
    EGC_GC;
    libxl__datacopier_buf *buf;
    /*
     * It is safe for this to be called immediately after _start, as
     * is documented in the public comment.  _start's caller must have
     * the ctx locked, so other threads don't get to mess with the
     * contents, and the fd events cannot happen reentrantly.  So we
     * are guaranteed to beat the first data from the read fd.
     */

    assert(len < dc->maxsz - dc->used);

    buf = libxl__zalloc(NOGC, sizeof(*buf) - sizeof(buf->buf) + len);
    buf->used = len;
    memcpy(buf->buf, data, len);

    dc->used += len;
    LIBXL_TAILQ_INSERT_TAIL(&dc->bufs, buf, entry);
}

static int datacopier_pollhup_handled(libxl__egc *egc,
                                      libxl__datacopier_state *dc,
                                      short revents, int onwrite)
{
    STATE_AO_GC(dc->ao);

    if (dc->callback_pollhup && (revents & POLLHUP)) {
        LOG(DEBUG, "received POLLHUP on %s during copy of %s",
            onwrite ? dc->writewhat : dc->readwhat,
            dc->copywhat);
        libxl__datacopier_kill(dc);
        dc->callback_pollhup(egc, dc, onwrite, -1);
        return 1;
    }
    return 0;
}

static void datacopier_readable(libxl__egc *egc, libxl__ev_fd *ev,
                                int fd, short events, short revents) {
    libxl__datacopier_state *dc = CONTAINER_OF(ev, *dc, toread);
    STATE_AO_GC(dc->ao);

    if (datacopier_pollhup_handled(egc, dc, revents, 0))
        return;

    if (revents & ~POLLIN) {
        LOG(ERROR, "unexpected poll event 0x%x (should be POLLIN)"
            " on %s during copy of %s", revents, dc->readwhat, dc->copywhat);
        datacopier_callback(egc, dc, -1, 0);
        return;
    }
    assert(revents & POLLIN);
    for (;;) {
        while (dc->used >= dc->maxsz) {
            libxl__datacopier_buf *rm = LIBXL_TAILQ_FIRST(&dc->bufs);
            dc->used -= rm->used;
            assert(dc->used >= 0);
            LIBXL_TAILQ_REMOVE(&dc->bufs, rm, entry);
            free(rm);
        }

        libxl__datacopier_buf *buf =
            LIBXL_TAILQ_LAST(&dc->bufs, libxl__datacopier_bufs);
        if (!buf || buf->used >= sizeof(buf->buf)) {
            buf = malloc(sizeof(*buf));
            if (!buf) libxl__alloc_failed(CTX, __func__, 1, sizeof(*buf));
            buf->used = 0;
            LIBXL_TAILQ_INSERT_TAIL(&dc->bufs, buf, entry);
        }
        int r = read(ev->fd,
                     buf->buf + buf->used,
                     sizeof(buf->buf) - buf->used);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EWOULDBLOCK) break;
            LOGE(ERROR, "error reading %s during copy of %s",
                 dc->readwhat, dc->copywhat);
            datacopier_callback(egc, dc, 0, errno);
            return;
        }
        if (r == 0) {
            libxl__ev_fd_deregister(gc, &dc->toread);
            break;
        }
        if (dc->log) {
            int wrote = fwrite(buf->buf + buf->used, 1, r, dc->log);
            if (wrote != r) {
                assert(ferror(dc->log));
                assert(errno);
                LOGE(ERROR, "error logging %s", dc->copywhat);
                datacopier_callback(egc, dc, 0, errno);
                return;
            }
        }
        buf->used += r;
        dc->used += r;
        assert(buf->used <= sizeof(buf->buf));
    }
    datacopier_check_state(egc, dc);
}

static void datacopier_writable(libxl__egc *egc, libxl__ev_fd *ev,
                                int fd, short events, short revents) {
    libxl__datacopier_state *dc = CONTAINER_OF(ev, *dc, towrite);
    STATE_AO_GC(dc->ao);

    if (datacopier_pollhup_handled(egc, dc, revents, 1))
        return;

    if (revents & ~POLLOUT) {
        LOG(ERROR, "unexpected poll event 0x%x (should be POLLOUT)"
            " on %s during copy of %s", revents, dc->writewhat, dc->copywhat);
        datacopier_callback(egc, dc, -1, 0);
        return;
    }
    assert(revents & POLLOUT);
    for (;;) {
        libxl__datacopier_buf *buf = LIBXL_TAILQ_FIRST(&dc->bufs);
        if (!buf)
            break;
        if (!buf->used) {
            LIBXL_TAILQ_REMOVE(&dc->bufs, buf, entry);
            free(buf);
            continue;
        }
        int r = write(ev->fd, buf->buf, buf->used);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EWOULDBLOCK) break;
            LOGE(ERROR, "error writing to %s during copy of %s",
                 dc->writewhat, dc->copywhat);
            datacopier_callback(egc, dc, 1, errno);
            return;
        }
        assert(r > 0);
        assert(r <= buf->used);
        buf->used -= r;
        dc->used -= r;
        assert(dc->used >= 0);
        memmove(buf->buf, buf->buf+r, buf->used);
    }
    datacopier_check_state(egc, dc);
}

int libxl__datacopier_start(libxl__datacopier_state *dc)
{
    int rc;
    STATE_AO_GC(dc->ao);

    libxl__datacopier_init(dc);

    rc = libxl__ev_fd_register(gc, &dc->toread, datacopier_readable,
                               dc->readfd, POLLIN);
    if (rc) goto out;

    rc = libxl__ev_fd_register(gc, &dc->towrite, datacopier_writable,
                               dc->writefd, POLLOUT);
    if (rc) goto out;

    return 0;

 out:
    libxl__datacopier_kill(dc);
    return rc;
}

/*----- openpty -----*/

/* implementation */
    
static void openpty_cleanup(libxl__openpty_state *op)
{
    int i;

    for (i=0; i<op->count; i++) {
        libxl__openpty_result *res = &op->results[i];
        libxl__carefd_close(res->master);  res->master = 0;
        libxl__carefd_close(res->slave);   res->slave = 0;
    }
}

static void openpty_exited(libxl__egc *egc, libxl__ev_child *child,
                           pid_t pid, int status) {
    libxl__openpty_state *op = CONTAINER_OF(child, *op, child);
    STATE_AO_GC(op->ao);

    if (status) {
        /* Perhaps the child gave us the fds and then exited nonzero.
         * Well that would be odd but we don't really care. */
        libxl_report_child_exitstatus(CTX, op->rc ? LIBXL__LOG_ERROR
                                                  : LIBXL__LOG_WARNING,
                                      "openpty child", pid, status);
    }
    if (op->rc)
        openpty_cleanup(op);
    op->callback(egc, op);
}

int libxl__openptys(libxl__openpty_state *op,
                    struct termios *termp,
                    struct winsize *winp) {
    /*
     * This is completely crazy.  openpty calls grantpt which the spec
     * says may fork, and may not be called with a SIGCHLD handler.
     * Now our application may have a SIGCHLD handler so that's bad.
     * We could perhaps block it but we'd need to block it on all
     * threads.  This is just Too Hard.
     *
     * So instead, we run openpty in a child process.  That child
     * process then of course has only our own thread and our own
     * signal handlers.  We pass the fds back.
     *
     * Since our only current caller actually wants two ptys, we
     * support calling openpty multiple times for a single fork.
     */
    STATE_AO_GC(op->ao);
    int count = op->count;
    int r, i, rc, sockets[2], ptyfds[count][2];
    libxl__carefd *for_child = 0;
    pid_t pid = -1;

    for (i=0; i<count; i++) {
        ptyfds[i][0] = ptyfds[i][1] = -1;
        libxl__openpty_result *res = &op->results[i];
        assert(!res->master);
        assert(!res->slave);
    }
    sockets[0] = sockets[1] = -1; /* 0 is for us, 1 for our child */

    libxl__carefd_begin();
    r = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
    if (r) { sockets[0] = sockets[1] = -1; }
    for_child = libxl__carefd_opened(CTX, sockets[1]);
    if (r) { LOGE(ERROR,"socketpair failed"); rc = ERROR_FAIL; goto out; }

    pid = libxl__ev_child_fork(gc, &op->child, openpty_exited);
    if (pid == -1) {
        rc = ERROR_FAIL;
        goto out;
    }

    if (!pid) {
        /* child */
        close(sockets[0]);
        signal(SIGCHLD, SIG_DFL);

        for (i=0; i<count; i++) {
            r = openpty(&ptyfds[i][0], &ptyfds[i][1], NULL, termp, winp);
            if (r) { LOGE(ERROR,"openpty failed"); _exit(-1); }
        }
        rc = libxl__sendmsg_fds(gc, sockets[1], "",1,
                                2*count, &ptyfds[0][0], "ptys");
        if (rc) { LOGE(ERROR,"sendmsg to parent failed"); _exit(-1); }
        _exit(0);
    }

    libxl__carefd_close(for_child);
    for_child = 0;

    /* this should be fast so do it synchronously */

    libxl__carefd_begin();
    char buf[1];
    rc = libxl__recvmsg_fds(gc, sockets[0], buf,1,
                            2*count, &ptyfds[0][0], "ptys");
    if (!rc) {
        for (i=0; i<count; i++) {
            libxl__openpty_result *res = &op->results[i];
            res->master = libxl__carefd_record(CTX, ptyfds[i][0]);
            res->slave =  libxl__carefd_record(CTX, ptyfds[i][1]);
        }
    }
    /* now the pty fds are in the carefds, if they were ever open */
    libxl__carefd_unlock();
    if (rc)
        goto out;

    rc = 0;

 out:
    if (sockets[0] >= 0) close(sockets[0]);
    libxl__carefd_close(for_child);
    if (libxl__ev_child_inuse(&op->child)) {
        op->rc = rc;
        /* we will get a callback when the child dies */
        return 0;
    }

    assert(rc);
    openpty_cleanup(op);
    return rc;
}

