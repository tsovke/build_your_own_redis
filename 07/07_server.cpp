#include <arpa/inet.h>
#include <assert.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <map>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
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
  (void)fcntl(fd, F_GETFL, 0);
  if (errno) {
    die("fcntl error");
  }
}

const size_t k_max_msg = 4096;

enum {
  STATE_REQ = 0,
  STATE_RES = 1,
  STATE_END = 2, // mark the connection for deletion

};

struct Conn {
  int fd = -1;
  uint32_t state = 0; // either STATE_REQ or STATE_RES
  // buffer for reading
  size_t rbuf_size = 0;
  uint8_t rbuf[4 + k_max_msg];
  // buffer for writing
  size_t wbuf_size = 0;
  size_t wbuf_sent = 0;
  uint8_t wbuf[4 + k_max_msg];
};

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
  // accept
  struct sockaddr_in client_addr = {};
  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    msg("accept() error");
    return -1; // error
  }

  // set the new connection fd to nonblocking mode
  fd_set_nb(connfd);
  // creating the struct Conn
  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  if (!conn) {
    close(connfd);
    return -1;
  }
  conn->fd = connfd;
  conn->state = STATE_REQ;
  conn->rbuf_size = 0;
  conn->wbuf_size = 0;
  conn->wbuf_sent = 0;
  conn_put(fd2conn, conn);
  return 0;
}

static void state_req(Conn *conn);
static void state_res(Conn *conn);

const size_t k_max_args = 1024;

static int32_t parse_req(const uint8_t *data, size_t len,
                         std::vector<std::string> &out) {
  if (len < 4) {
    return -1;
  }

  uint32_t n = 0;
  memcpy(&n, &data[0], 4);
  if (n > k_max_args) {
    return -1;
  }
  size_t pos = 4;
  while (n--) {
    if (pos + 4 > len) {
      return -1;
    }
    uint32_t sz = 0;
    memcpy(&sz, &data[pos], 4);
    if (pos + 4 + sz > len) {
      return -1;
    }
    out.push_back(std::string((char *)&data[pos + 4], sz));
    pos += 4 + sz;
  }

  if (pos != len) {
    return -1; // trailing garbage
  }
  return 0;
}

enum {
  RES_OK = 0,
  RES_ERR = 1,
  RES_NX = 2,
};

// The data structure for the key space. This is just a placeholder
// until we implement a hashtable in the next chapter.
static std::map<std::string, std::string> g_map;
static uint32_t do_get(const std::vector<std::string> &cmd, uint8_t *res,
                       uint32_t *reslen) {
  if (!g_map.count(cmd[1])) {
    return RES_NX;
  }

  std::string &val = g_map[cmd[1]];
  assert(val.size() <= k_max_msg);
  memcpy(res, val.data(), val.size());
  *reslen = (uint32_t)val.size();
  return RES_OK;
}

static uint32_t do_set(const std::vector<std::string> &cmd, uint8_t *res,
                       uint32_t *reslen) {
  (void)res;
  (void)reslen;
  g_map[cmd[1]] = cmd[2];
  return RES_OK;
}

static uint32_t do_del(const std::vector<std::string> &cmd, uint8_t *res,
                       uint32_t *reslen) {
  (void)res;
  (void)reslen;
  g_map.erase(cmd[1]);
  return RES_OK;
}

static bool cmd_is(const std::string &word, const char *cmd) {
  return 0 == strcasecmp(word.c_str(), cmd);
}

static int32_t do_request(const uint8_t *req, uint32_t reqlen,
                          uint32_t *rescode, uint8_t *res, uint32_t *reslen) {
  std::vector<std::string> cmd;
  if (0 != parse_req(req, reqlen, cmd)) {
    msg("bad req");
    return -1;
  }
  if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
    *rescode = do_get(cmd, res, reslen);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
    *rescode = do_set(cmd, res, reslen);
  }else if (cmd.size()==2&&cmd_is(cmd[0],"del" ){
    *rescode = do_del(cmd, res, reslen);
    
  }else{
    // cmd is not recognized
    *rescode = RES_ERR;
    const char *msg = "Unknown cmd";
    strcpy((char *)res, msg);
    *reslen = strlen(msg);

    return 0;
  }
  return 0;
}
