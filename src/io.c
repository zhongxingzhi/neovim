#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <uv.h>

#include "io.h"
#include "util.h"

#define BUF_SIZE 4096


static uv_thread_t io_thread;
static uv_mutex_t io_mutex;
static uv_cond_t io_cond;
static uv_async_t read_wake_async;
static uv_pipe_t stdin_pipe, stdout_pipe;
static struct {
  unsigned int wpos, rpos;
  char_u data[BUF_SIZE];
} in_buffer;
bool reading = false;


/* Private */
static void io_main(void *);
static void loop_running(uv_idle_t *, int);
static void read_wake(uv_async_t *, int);
static void alloc_buffer_cb(uv_handle_t *, size_t, uv_buf_t *);
static void read_cb(uv_stream_t *, ssize_t, const uv_buf_t *);
static void io_lock();
static void io_unlock();
static void io_wait();
static void io_timedwait(long ms);
static void io_signal();


/* Called at startup to setup the background thread that will handle all
 * events and translate to keys. */
void io_init() {
  uv_mutex_init(&io_mutex);
  uv_cond_init(&io_cond);
  io_lock();
  /* The event loop runs in a background thread */
  uv_thread_create(&io_thread, io_main, NULL);
  /* Wait for the loop thread to be ready */
  io_wait();
  io_unlock();
}


/*
 * TODO:
 *  - Handle resize(SIGWINCH)
 *  - Handle cursorhold(probably refactor the current implementation of
 *    treating it as a key)
 *  - Refactor tb_change_cnt, its related to the typebuffer which will
 *    also be removed in the future
 */
int mch_inchar(char_u *buf, int maxlen, long wtime, int tb_change_cnt) {
  int rv;

  UNUSED(tb_change_cnt);
  io_lock();

  if (!reading) {
    uv_async_send(&read_wake_async);
    reading = true;
  }

  if (in_buffer.rpos == in_buffer.wpos) {
    if (wtime == -1) io_wait();
    else io_timedwait(wtime);
  }

  /* Copy at most 'maxlen' to the buffer argument */
  rv = 0;
  while (in_buffer.rpos < in_buffer.wpos && rv < maxlen)
    buf[rv++] = in_buffer.data[in_buffer.rpos++];

  io_unlock();

  return rv;
}


int mch_char_avail() {
  return in_buffer.rpos < in_buffer.wpos;
}

void mch_delay(long msec, int ignoreinput) {
  // int old_tmode;

  // if (ignoreinput) {
  //   /* Go to cooked mode without echo, to allow SIGINT interrupting us
  //    * here.  But we don't want QUIT to kill us (CTRL-\ used in a
  //    * shell may produce SIGQUIT). */
  //   in_mch_delay = TRUE;
  //   old_tmode = curr_tmode;
  //   if (curr_tmode == TMODE_RAW)
  //     settmode(TMODE_SLEEP);

  //   /*
  //    * Everybody sleeps in a different way...
  //    * Prefer nanosleep(), some versions of usleep() can only sleep up to
  //    * one second.
  //    */
// #ifdef HAVE_NANOSLEEP
  //   {
  //     struct timespec ts;

  //     ts.tv_sec = msec / 1000;
  //     ts.tv_nsec = (msec % 1000) * 1000000;
  //     (void)nanosleep(&ts, NULL);
  //   }
// #else
// # ifdef HAVE_USLEEP
  //   while (msec >= 1000) {
  //     usleep((unsigned int)(999 * 1000));
  //     msec -= 999;
  //   }
  //   usleep((unsigned int)(msec * 1000));
// # else
// #  ifndef HAVE_SELECT
  //   poll(NULL, 0, (int)msec);
// #  else
  //   {
  //     struct timeval tv;

  //     tv.tv_sec = msec / 1000;
  //     tv.tv_usec = (msec % 1000) * 1000;
  //     /*
  //      * NOTE: Solaris 2.6 has a bug that makes select() hang here.  Get
  //      * a patch from Sun to fix this.  Reported by Gunnar Pedersen.
  //      */
  //     select(0, NULL, NULL, NULL, &tv);
  //   }
// #  endif /* HAVE_SELECT */
// # endif /* HAVE_NANOSLEEP */
// #endif /* HAVE_USLEEP */

  //   settmode(old_tmode);
  //   in_mch_delay = FALSE;
  // } else
  //   WaitForChar(msec);
  UNUSED(ignoreinput);
  io_timedwait(msec);
}


static void io_main(void *arg) {
  uv_idle_t idler;

  memset(&in_buffer, 0, sizeof(in_buffer));

  UNUSED(arg);
  /* use default loop */
  uv_loop_t *loop = uv_default_loop();
  /* Idler for signaling the main thread when the loop is running */
  uv_idle_init(loop, &idler);
  uv_idle_start(&idler, loop_running);
  /* Async watcher used by the main thread to resume reading */
  uv_async_init(loop, &read_wake_async, read_wake);
  /* stdin */
  uv_pipe_init(loop, &stdin_pipe, 0);
  uv_pipe_open(&stdin_pipe, 0);
  /* stdout */
  uv_pipe_init(loop, &stdout_pipe, 0);
  uv_pipe_open(&stdout_pipe, 1);
  /* start processing events */
  uv_run(loop, UV_RUN_DEFAULT);
}


/* Signal the main thread that the loop started running */
static void loop_running(uv_idle_t *handle, int status) {
  uv_idle_stop(handle);
  io_lock();
  io_signal();
  io_unlock();
}


/* Signal tell loop to continue reading stdin */
static void read_wake(uv_async_t *handle, int status) {
  UNUSED(handle);
  UNUSED(status);
  uv_read_start((uv_stream_t *)&stdin_pipe, alloc_buffer_cb, read_cb);
}


/* Called by libuv to allocate memory for reading. This uses a static buffer */
static void alloc_buffer_cb(uv_handle_t *handle, size_t ssize, uv_buf_t *rv) {
  int wpos;
  UNUSED(handle);
  io_lock();
  wpos = in_buffer.wpos;
  io_unlock();
  if (wpos == BUF_SIZE) {
    /* No more space in buffer */
    rv->len = 0;
    return;
  }
  if (BUF_SIZE < (wpos + ssize))
    ssize = BUF_SIZE - wpos;
  rv->base = (char *)(in_buffer.data + wpos);
  rv->len = ssize;
}


/* This is only used to check how many bytes were read or if an error
 * occurred. If the static buffer is full(wpos == BUF_SIZE) try to move
 * the data to free space, or stop reading. */
static void read_cb(uv_stream_t *s, ssize_t cnt, const uv_buf_t *buf) {
  int move_count;
  UNUSED(s);
  UNUSED(buf); /* Data is already on the static buffer */
  if (cnt < 0) {
    if (cnt == UV_EOF) {
      uv_unref((uv_handle_t *)&stdin_pipe);
    } else if (cnt == UV_ENOBUFS) {
      /* Out of space in internal buffer, move data to the 'left' as much
       * as possible. If we cant move anything, stop reading for now. */
      io_lock();
      if (in_buffer.rpos == 0)
      {
        reading = false;
        io_unlock();
        uv_read_stop((uv_stream_t *)&stdin_pipe);
      }
      move_count = BUF_SIZE - in_buffer.rpos;
      memmove(in_buffer.data, in_buffer.data + in_buffer.rpos, move_count);
      in_buffer.wpos -= in_buffer.rpos;
      in_buffer.rpos = 0;
      io_unlock();
    }
    else {
      fprintf(stderr, "Unexpected error %s\n", uv_strerror(cnt));
    }
    return;
  }
  io_lock();
  in_buffer.wpos += cnt;
  io_signal();
  io_unlock();
}


/* Helpers for dealing with io synchronization */
static void io_lock() {
  uv_mutex_lock(&io_mutex);
}


static void io_unlock() {
  uv_mutex_unlock(&io_mutex);
}


static void io_wait() {
  uv_cond_wait(&io_cond, &io_mutex);
}


static void io_timedwait(long ms) {
  (void)uv_cond_timedwait(&io_cond, &io_mutex, ms * 1000000);
}


static void io_signal() {
  uv_cond_signal(&io_cond);
}
