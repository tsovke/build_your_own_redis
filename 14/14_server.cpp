#include <arpa/inet.h>
#include <assert.h>
#include <bits/types/error_t.h>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <errno.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <vector>
// proj
#include "common.h"
#include "hashtable.h"
#include "heap.h"
#include "list.h"
#include "thread_pool.h"
#include "zset.h"

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

static uint32_t get_monotonic_usec() {
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
  // a map of all client connections,keyed by fd
  std::vector<Conn *> fd2conn;
  // timers for idle connections
  DList idle_list;
  // timers for TTLs
  std::vector<HeapItem> heap;
  // the thread pool
  TheadPool tp;
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
  // buffer for writing
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
  // for TTLs
  size_t heap_idx = -1;
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
static void out_dbl(std::string &out, int64_t val) {
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

// set or remove the TTL
static void entry_set_ttl(Entry *ent, int64_t ttl_ms) {
  if (ttl_ms < 0 && ent->heap_idx != (size_t)-1) {
    // erase an item from the heap
    // by replacing it with the last item in the array.
    size_t pos = ent->heap_idx;
    g_data.heap[pos] = g_data.heap.back();
    g_data.heap.pop_back();
    if (pos < g_data.heap.size()) {
      heap_update(g_data.heap.data(), pos, g_data.heap.size());
    }
    ent->heap_idx = -1;
  } else if (ttl_ms >= 0) {
    size_t pos = ent->heap_idx;
    if (pos == (size_t)-1) {
      // add an new item to the heap
      HeapItem item;
      item.ref = &ent->heap_idx;
      g_data.heap.push_back(item);
      pos = g_data.heap.size() - 1;
    }
    g_data.heap[pos].val = get_monotonic_usec() + (uint64_t)ttl_ms * 1000;
    heap_update(g_data.heap.data(), pos, g_data.heap.size());
  }
}

static bool str2int(const std::string &s, int64_t &out) {
  char *endp = NULL;
  out = strtoll(s.c_str(), &endp, 10);
  return endp == s.c_str() + s.size();
}

static void do_expire(std::vector<std::string> &cmd, std::string &out) {
  int64_t ttl_ms = 0;
  if (!str2int(cmd[2], ttl_ms)) {
    return out_err(out, ERR_ARG, "expect int64");
  }

  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (node) {
    Entry *ent = container_of(node, Entry, node);
    entry_set_ttl(ent, ttl_ms);
  }
  return out_int(out, node ? 1 : 0);
}

static void do_ttl(std::vector<std::string> &cmd, std::string &out) {

  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!node) {
    return out_int(out, -2);
  }

  Entry *ent = container_of(node, Entry, node);
  if (ent->heap_idx == (size_t)-1) {
    return out_int(out, -1);
  }

  uint64_t expire_at = g_data.heap[ent->heap_idx].val;
  uint64_t now_us = get_monotonic_usec();
  return out_int(out, expire_at > now_us ? (expire_at - now_us) / 1000 : 0);
}

// deallocate the key immediately
static void entry_destroy(Entry *ent) {
  switch (ent->type) {
  case T_ZSET:
    zset_dispose(ent->zset);
    delete ent->zset;
    break;
  }
  delete ent;
}

static void entry_del_async(void *arg) { entry_destroy((Entry *)arg); }

// dispose the entry after it got detached from the key space
static void entry_del(Entry *ent) {
  entry_set_ttl(ent, -1);

  const size_t k_large_container_size = 10000;
  bool too_big = false;
  switch (ent->type) {
  case T_ZSET:
    too_big = hm_size(&ent->zset->hmap) > k_large_container_size;
    break;
  }
  if (too_big) {
    thread_pool_queue(&g_data.tp, &entry_del_async, ent);
  } else {
    entry_destroy(ent);
  }
}

static void do_del(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
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

// zadd zset score name
static void do_zadd(std::vector<std::string> &cmd, std::string &out) {
  double score = 0;
  if (!str2dbl(cmd[2], score)) {
    return out_err(out, ERR_ARG, "expect fp number");
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

// zscore zset name
static void do_zscore(std::vector<std::string> &cmd, std::string &out) {
  Entry *ent = NULL;
  if (!expect_zset(out, cmd[1], &ent)) {
    return;
  }

  const std::string &name = cmd[2];
  ZNode *znode = zset_lookup(ent->zset, name.data(), name.size());
  return znode ? out_dbl(out, znode->score) : out_nil(out);
}

// zquery zset score name offset limit
static void do_zquery(std::vector<std::string> &cmd, std::string &out) {
  // parse args
  double score = 0;
  if (!str2dbl(cmd[2], score)) {
    return out_err(out, ERR_ARG, "expect fp number");
  }
  const std::string &name = cmd[3];
  int64_t offset = 0;
  int64_t limit = 0;
  if (!str2int(cmd[4], offset)) {
    return out_err(out, ERR_ARG, "expect int");
  }
  if (!str2int(cmd[5], limit)) {
    return out_err(out, ERR_ARG, "expect int");
  }

  // get the zset
  Entry *ent = NULL;
  if (!expect_zset(out, cmd[1], &ent)) {
    if (out[0] == SER_NIL) {
      out.clear();
      out_arr(out, 0);
    }
    return;
  }

  // look up the tuple
  if (limit <= 0) {
    return out_arr(out, 0);
  }
  ZNode *znode = zset_query(ent->zset, score, name.data(), name.size());
  znode = znode_offset(znode, offset);

  // output
  void *arr = begin_arr(out);
  uint32_t n = 0;
  while (znode && (int64_t)n < limit) {
    out_str(out, znode->name, znode->len);
    out_dbl(out, znode->score);
    znode = znode_offset(znode, +1);
    n += 2;
  }
  end_arr(out, arr, n);
}

static bool cmd_is(const std::string &word, const char *cmd) {
  return 0 == strcasecmp(word.c_str(), cmd);
}

static void do_request(std::vector<std::string> &cmd, std::string &out) {
  if (cmd.size() == 1 && cmd_is(cmd[0], "keys")) {
    do_keys(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
    do_get(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
    do_set(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "del")) {
    do_del(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "pexpire")) {
    do_expire(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "pttl")) {
    do_ttl(cmd, out);
  } else if (cmd.size() == 4 && cmd_is(cmd[0], "zadd")) {
    do_zadd(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "zrem")) {
    do_zrem(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "zscore")) {
    do_zscore(cmd, out);
  } else if (cmd.size() == 6 && cmd_is(cmd[0], "zquery")) {
    do_zquery(cmd, out);
  } else {
    // cmd is not recognized
    out_err(out, ERR_UNKNOWN, "Unknown cmd");
  }
}

static bool try_one_request(Conn *conn) {
  // try to parse a requst from the buffer
  if (conn->rbuf_size < 4) {
    // not enough data in the buffer. Will retry in the next iteration
    return false;
  }
  uint32_t len = 0;
  memcpy(&len, &conn->rbuf[0], 4);
  if (len > k_max_msg) {
    msg("too long");
    conn->state = STATE_END;
    return false;
  }
  if (4 + len > conn->rbuf_size) {
    // not enough data in the buffer. Will retry in the next iteration
    return false;
  }

  // parse the request
  std::vector<std::string> cmd;
  if (0 != parse_req(&conn->rbuf[4], len, cmd)) {
    msg("bad req");
    conn->state = STATE_END;
    return false;
  }

  // got one request, generate the response.
  std::string out;
  do_request(cmd, out);

  // pack the response into the buffer
  if (4 + out.size() > k_max_msg) {
    out.clear();
    out_err(out, ERR_2BIG, "response is too big");
  }
  uint32_t wlen = (uint32_t)out.size();
  memcpy(&conn->wbuf[0], &wlen, 4);
  memcpy(&conn->wbuf[4], out.data(), out.size());
  conn->wbuf_size = 4 + wlen;

  // remove the request from the buffer.
  // note: frequent memmove is inefficient.
  // note: need better handling for production code.
  size_t remain = conn->rbuf_size - 4 - len;
  if (remain) {
    memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
  }
  conn->rbuf_size = remain;

  // change state
  conn->state = STATE_RES;
  state_res(conn);

  // continue the outer loop if the request was fully processed
  return (conn->state == STATE_REQ);
}

static bool try_fill_buffer(Conn *conn) {
  // try to fill the buffer
  assert(conn->rbuf_size < sizeof(conn->rbuf));
  ssize_t rv = 0;
  do {
    size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
    rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
  } while (rv < 0 && errno == EINTR);
  if (rv < 0 && errno == EAGAIN) {
    // got EAGAIN, stop.
    return false;
  }
  if (rv < 0) {
    msg("read() error");
    conn->state = STATE_END;
    return false;
  }
  if (rv == 0) {
    if (conn->rbuf_size > 0) {
      msg("unexpected EOF");
    } else {
      msg("EOF");
    }
    conn->state = STATE_END;
    return false;
  }

  conn->rbuf_size += (size_t)rv;
  assert(conn->rbuf_size <= sizeof(conn->rbuf));

  // Try to process requests one by one.
  // Why is there a loop? Please read the explanation of "pipelining".
  while (try_one_request(conn)) {
  }
  return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn) {
  while (try_fill_buffer(conn)) {
  }
}
