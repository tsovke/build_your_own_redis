#include <arpa/inet.h>
#include <assert.h>
#include <bits/types/error_t.h>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/ip.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <time.h>
#include <type_traits>
#include <unistd.h>
#include <vector>
// proj
#include "common.h"
#include "hashtable.h"
#include "heap.h"
#include "list.h"
#include "zset.h"

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

static uint64_t get_monotonic_usec() {
  timespec tv = {0, 0};
  clock_gettime(CLOCK_MONOTONIC, &tv);
  return uint64_t(tv.tv_sec) * 1000000 + tv.tv_nsec / 1000;
}

static void fd_set_nb(int fd) {
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if (errno) {
    die("fcntl error");
    return;
  }

  flags |= O_NONBLOCK;

  errno = 0;
  (void)fcntl(fd, F_SETFL, flags);
  if (errno) {
    die("fcntl error");
  }
}

struct Conn;

// global variables
static struct {
  HMap db;
  // a map of all client connections, keyed by fd
  std::vector<Conn *> fd2conn;
  // timers for idle connections
  DList idle_list;
  // timers for TTLs
  std::vector<HeapItem> heap;
} g_data;

const size_t k_max_msg = 4096;

enum {
  STATE_REQ = 0,
  STATE_RES = 1,
  STATE_END = 2, // mark the connection for deletion
};
