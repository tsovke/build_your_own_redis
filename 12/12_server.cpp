#include <arpa/inet.h>
#include <asm-generic/errno.h>
#include <assert.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <vector>
// proj
#include "common.h"
#include "hashtable.h"
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
} g_data;

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
  // buffer writing

  size_t wbuf_size = 0;
  size_t wbuf_sent = 0;

  uint8_t wbuf[4 + k_max_msg];
  uint64_t idle_start = 0;
  // timer
  DList idle_list;
};

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
  if (fd2conn.size() <= (size_t)conn->fd) {
    fd2conn.resize(conn->fd + 1);
  }
  fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(int fd) {
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
  conn->idle_start = get_monotonic_usec();
  dlist_insert_before(&g_data.idle_list, &conn->idle_list);
  conn_put(g_data.fd2conn, conn);
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
  T_STR = 0,
  T_ZSET = 1,
};

// the structure for the key
struct Entry {
  struct HNode node;
  std::string key;
  std::string val;
  uint32_t type = 0;
  ZSet *zset = NULL;
};

static bool entry_eq(HNode *lhs, HNode *rhs) {

  struct Entry *le = container_of(lhs, struct Entry, node);
  struct Entry *re = container_of(rhs, struct Entry, node);
  return le->key == re->key;
}

enum {
  ERR_UNKNOWN = 1,
  ERR_2BIG = 2,
  ERR_TYPE = 3,
  ERR_ARG = 4,
};

static void out_nil(std::string &out) { out.push_back(SER_NIL); }

static void out_str(std::string &out, const char *s, size_t size) {
  out.push_back(SER_STR);
  uint32_t len = (uint32_t)size;
  out.append((char *)&len, 4);
  out.append(s, len);
}

static void out_str(std::string &out, const std::string &val) {
  return out_str(out, val.data(), val.size());
}

static void out_int(std::string &out, int64_t val) {
  out.push_back(SER_INT);
  out.append((char *)&val, 8);
}
static void out_dbl(std::string &out, double val) {
  out.push_back(SER_DBL);
  out.append((char *)&val, 8);
}

static void out_err(std::string &out, int32_t code, const std::string &msg) {
  out.push_back(SER_ERR);
  out.append((char *)&code, 4);
  uint32_t len = (uint32_t)msg.size();
  out.append((char *)&len, 4);
  out.append(msg);
}

static void out_arr(std::string &out, uint32_t n) {
  out.push_back(SER_ARR);
  out.append((char *)&n, 4);
}

static void *begin_arr(std::string &out) {
  out.push_back(SER_ARR);
  out.append("\0\0\0\0", 4);       // filled in end_arr()
  return (void *)(out.size() - 4); // the `ctx` arg
}

static void end_arr(std::string &out, void *ctx, uint32_t n) {
  size_t pos = (size_t)ctx;
  assert(out[pos - 1] == SER_ARR);
  memcpy(&out[pos], &n, 4);
}

static void do_get(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!node) {
    return out_nil(out);
  }
  Entry *ent = container_of(node, Entry, node);
  if (ent->type != T_STR) {
    return out_err(out, ERR_TYPE, "expect string type");
  }
  return out_str(out, ent->val);
}

static void do_set(std::vector<std::string> &cmd, std::string &out) {

  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (node) {

    Entry *ent = container_of(node, Entry, node);
    if (ent->type != T_STR) {

      return out_err(out, ERR_TYPE, "expect string type");
    }
    ent->val.swap(cmd[2]);
  } else {
    Entry *ent = new Entry();
    ent->key.swap(key.key);
    ent->node.hcode = key.node.hcode;
    ent->val.swap(cmd[2]);
    hm_insert(&g_data.db, &ent->node);
  }
  return out_nil(out);
}

static void entry_del(Entry *ent) {
  switch (ent->type) {
  case T_ZSET:
    zset_dispose(ent->zset);
    delete ent->zset;
    break;
  }
  delete ent;
}

static void do_del(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);
  if (node) {
    entry_del(container_of(node, Entry, node));
  }
  return out_int(out, node ? 1 : 0);
}

static void h_scan(HTab *tab, void (*f)(HNode *, void *), void *arg) {
  if (tab->size == 0) {
    return;
  }
  for (size_t i = 0; i < tab->mask + 1; ++i) {
    HNode *node = tab->tab[i];
    while (node) {
      f(node, arg);
      node = node->next;
    }
  }
}

static void cb_scan(HNode *node, void *arg) {
  std::string &out = *(std::string *)arg;
  out_str(out, container_of(node, Entry, node)->key);
}

static void do_keys(std::vector<std::string> &cmd, std::string &out) {
  (void)cmd;
  out_arr(out, (uint32_t)hm_size(&g_data.db));
  h_scan(&g_data.db.ht1, &cb_scan, &out);
  h_scan(&g_data.db.ht2, &cb_scan, &out);
}

static bool str2dbl(const std::string &s, double &out) {
  char *endp = NULL;
  out = strtod(s.c_str(), &endp);
  return endp == s.c_str() + s.size() && !std::isnan(out);
}
static bool str2int(const std::string &s, int64_t &out) {
  char *endp = NULL;
  out = strtoll(s.c_str(), &endp, 10);
  return endp == s.c_str() + s.size();
}

// zadd zset score name

static void do_zadd(std::vector<std::string> &cmd, std::string &out) {
  double score = 0;
  if (!str2dbl(cmd[2], score)) {

    return out_err(out, ERR_TYPE, "expect fp number");
  }

  // look up or create the zset

  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *hnode = hm_lookup(&g_data.db, &key.node, &entry_eq);

  Entry *ent = NULL;
  if (!hnode) {
    ent = new Entry();
    ent->key.swap(key.key);
    ent->node.hcode = key.node.hcode;
    ent->type = T_ZSET;
    ent->zset = new ZSet();
    hm_insert(&g_data.db, &ent->node);
  } else {
    ent = container_of(hnode, Entry, node);
    if (ent->type != T_ZSET) {
      return out_err(out, ERR_TYPE, "expect zset");
    }
  }

  // add or update the tuple
  const std::string &name = cmd[3];
  bool added = zset_add(ent->zset, name.data(), name.size(), score);
  return out_int(out, (int64_t)added);
}

static bool expect_zset(std::string &out, std::string &s, Entry **ent) {
  Entry key;
  key.key.swap(s);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  HNode *hnode = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!hnode) {
    out_nil(out);
    return false;
  }

  *ent = container_of(hnode, Entry, node);
  if ((*ent)->type != T_ZSET) {
    out_err(out, ERR_TYPE, "expect zset");
    return false;
  }
  return true;
}

// zrem zset name
static void do_zrem(std::vector<std::string> &cmd, std::string &out) {
  Entry *ent = NULL;
  if (!expect_zset(out, cmd[1], &ent)) {
    return;
  }
  const std::string &name = cmd[2];
  ZNode *znode = zset_pop(ent->zset, name.data(), name.size());
  if (znode) {
    znode_del(znode);
  }
  return out_int(out, znode ? 1 : 0);
}

static void do_zscore(std::vector<std::string> &cmd, std::string &out) {

  Entry *ent = NULL;
  if (!expect_zset(out, cmd[1], &ent)) {
    return;
  }
  const std::string &name = cmd[2];
  ZNode *znode = zset_lookup(ent->zset, name.data(), name.size());
  return znode ? out_dbl(out, znode->score) : out_nil(out);
}
