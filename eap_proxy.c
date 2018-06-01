#include <errno.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Idea: capture auth packets and forward
 */

#define ASSERT_ERRNO(x)				\
  do {						\
    if (!(x)) {					\
      perror(#x " failed");			\
      exit(1);					\
    }						\
  } while(0);

#define ASSERT(x)				\
  do {						\
    if (!(x)) {					\
      fprintf(stderr, #x " failed");		\
      exit(1);					\
    }						\
  } while(0);

void usage(const char* me) {
  printf("usage: %s ont_iface_num int_iface_num\n", me);
  printf("  e.g. %s eth0 eth1\n", me);
  exit(0);
}

int open_iface(const char* ethname) {
  int fd = socket(PF_PACKET, SOCK_RAW, htons(0x888e));
  ASSERT_ERRNO(fd >= 0);
  // bind
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ethname, sizeof(ifr.ifr_name));
  if ((ioctl(fd , SIOCGIFINDEX , &ifr)) == -1)
  int index = 0;
  

  // Set promiscuous mode:
  int rc = ioctl(fd, SIOCGIFFLAGS, &ifr);
  ASSERT_ERRNO(rc >= 0);
  ifr.ifr_flags |=  IFF_PROMISC;
  rc = ioctl(fd, SIOCSIFFLAGS, &ifr);
  ASSERT_ERRNO(rc >= 0);

  return fd;
}

void move_packet(int from, int to) {
  unsigned char buf[2048];
  int rc = recv(from, buf, sizeof(buf), 0);
  ASSERT_ERRNO(rc > 0);
  int send_rc = send(to, buf, rc, 0);
  ASSERT_ERRNO(rc == send_rc);
  printf("just sent a packet! fds: %d -> %d\n", from, to);
}

int main(int argc, char** argv) {
  if (argc < 3)
    usage();
  int s_ont = open_iface(argv[1]);
  int s_int = open_iface(argv[2]);
  int max_fd = max(s_ont, s_int);

  // route the packets
  fd_set read_fds;
  while (1) {
    FD_ZERO(&read_fds);
    FD_SET(s_ont, &read_fds);
    FD_SET(s_int, &read_fds);
    int rc = select(max_fd, &read_fds, NULL, NULL, NULL);
    ASSERT_ERRNO(rc >= 0);
    ASSERT(rc > 0);
    if (FD_ISSET(s_ont, &read_fds))
      move_packet(s_ont, s_int);
    if (FD_ISSET(s_int, &read_fds))
      move_packet(s_int, s_ont);
  }
}
