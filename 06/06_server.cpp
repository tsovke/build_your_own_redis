#include <assert.h>
#include <cstddef>
#include <cstdlib>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>


static void msg(cosnt char *msg){
  fprintf(stderr,"%s\n",msg );
}

static void die(const char *msg){
  int err=errno;
  fprintf(stderr,"[%d] %s\n",err,msg );
  abort();
}

static void fd_set_nb(int fd){
  errno=0;
  int flags=fcntl(fd,F_GETFL,0 );
  if(errno){
    die("fcntl error");
    return;
  }

  flags|= O_NONBLOCK;

  errno =0;
  (void)fcntl(fd,F_SETFL,flags );
  if(errno){
    die("fcntl error");
  }
}

const size_t k_max_msg=4096;

enum {
  STATE_REQ=0,
  STATE_RES=1,
  STATE_END=2,//mark the connection for deletion
};

struct Conn{
  int fd=-1;
  uint32_t state=0;//either STATE_REQ or STATE_RES
  //buffer for reading
  size_t rbuf_size=0;
  uint8_t rbuf[4+k_max_msg];
  //buffer for writing
  size_t wbuf_size=0;
  size_t wbuf_sent=0;
  uint8_t wbuf[4+k_max_msg];
  
};

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn){
  if(fd2conn.size()<=(size_t)conn->fd){
    fd2conn.resize(conn->fd+1);
  }
  fd2conn[conn->fd]=conn;
}

