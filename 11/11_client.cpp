#include <arpa/inet.h>
#include <assert.h>
#include <bits/types/error_t.h>
#include <cstdlib>
#include <errno.h>
#include <netinet/ip.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
// proj
#include "common.h"

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void die(const char *msg) {
  int err = errno;

  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}
