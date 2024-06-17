#include "thread_pool.h"
#include <assert.h>
#include <cstddef>
#include <pthread.h>

static void *worker(void *arg) {
  TheadPool *tp = (TheadPool *)arg;
  while (true) {
    pthread_mutex_lock(&tp->mu);
    // wait for the condition: a non-empty queue
    while (tp->queue.empty()) {
      pthread_cond_wait(&tp->not_empty, &tp->mu);
    }

    // got the job
    Work w = tp->queue.front();
    tp->queue.pop_front();
    pthread_mutex_unlock(&tp->mu);

    // do the work
    w.f(w.arg);
  }
  return NULL;
}
