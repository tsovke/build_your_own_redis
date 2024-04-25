#include <arpa/inet.h>
#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <errno.h>
#include <netinet/ip.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

static int32_t read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = read(fd, buf, n);
    if (rv <= 0) {
      return -1; // error, or unexpected EOF
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = write(fd, buf, n);
    if (rv <= 0) {
      return -1; // error
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

const size_t k_max_msg = 4096;

static int32_t send_req(int fd, const std::vector<std::string> &cmd) {
  uint32_t len = 4;
  for (const std::string &s : cmd) {
    len += 4 + s.size();
  }
  if (len > k_max_msg) {
    return -1;
  }

  char wbuf[4 + k_max_msg];
  memcpy(&wbuf[0], &len, 4); // assume little endian
  uint32_t n = cmd.size();
  memcpy(&wbuf[4], &n, 4);
  size_t cur = 8;
  for (const std::string &s : cmd) {
    uint32_t p = (uint32_t)s.size();
    memcpy(&wbuf[cur], &p, 4);
    memcpy(&wbuf[cur + 4], s.data(), s.size());
    cur += 4 + s.size();
  }
  return write_all(fd, wbuf, 4 + len);
}

enum {
  SER_NIL = 0,
  SER_ERR = 1,
  SER_STR = 2,
  SER_INT = 3,
  SER_ARR = 4,
};

static int32_t on_response(const uint8_t *data,size_t size){
  if(size <1){
    msg("bad response");
    return -1;
  }
  switch(data[0]){
    case SER_NIL:
    printf("(nil)\n");
    return 1;
    case SER_ERR:
      if (size<1+8) {
        msg("bad response");
        return -1;
      }
      {
        int32_t code=0;
        uint32_t len=0;
        memcpy(&code,&data[1] ,4 );
        memcpy(&len,&data[1+4] ,4 );
        if (size<1+8+len) {
          msg("bad response");return -1;
        }
        printf("(err) %d %.*s.\n",code,len,&data[1+8]);
        return 1+8+len;
      }
    case SER_STR:
      if (size<1+4) {
        msg("bad response");
        return -1;
      }{
        uint32_t len=0;
        memcpy(&len,&data[1] ,4 );
        if (size <1+4+len) {
          msg("bad response");
          return -1;        
        }
        printf("(str) %.*s\n",len,&data[1+4]);
        return 1+4+len;
      }
    case SER_INT:
      if (size<1+8) {
        msg("bad response");
        return -1;
      }
      {
        int64_t val=0;
        
      }
    case SER_ARR:
  }
}
