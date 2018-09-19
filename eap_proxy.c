#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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
      fprintf(stderr, #x " failed\n");		\
      exit(1);					\
    }						\
  } while(0);

#define MAX(a, b) ((a) > (b) ? (a) : (b))

const int kMaxIfaceIdx = 256;  // big enough?

void usage(const char* me) {
  printf("usage: %s -l  # list interfaces\n", me);
  printf("       %s [-m|-n] ont_iface_num int_iface_num  # run proxy\n", me);
  printf("         -m: Use multicase mode rather than promiscuous\n");
  printf("         -n: Don't change interface modes (defaults to promiscuous)\n");
  printf("  e.g. %s -m eth0 eth1\n", me);
  exit(0);
}

int open_iface(const char* ethname, int promiscuous, int multicast) {
  int fd = socket(PF_PACKET, SOCK_RAW, htons(0x888e));
  ASSERT_ERRNO(fd >= 0);
  // bind
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ethname, sizeof(ifr.ifr_name) - 1);
  if ((ioctl(fd , SIOCGIFINDEX , &ifr)) == -1) {
    perror("ioctl(SIOCGIFINXEX):");
    printf("unable to get index of %s\n", ethname);
    return -1;
  }
  printf("index is %d\n", ifr.ifr_ifindex);
  struct sockaddr_ll sll = {0};
  sll.sll_family = AF_PACKET;
  sll.sll_ifindex = ifr.ifr_ifindex;
  sll.sll_protocol = htons(0x888e);
  if (bind(fd, (struct sockaddr*)&sll, sizeof(sll)) < 0) {
    perror("bind"); 
    printf("unable to bind to %s\n", ethname);
    return -1;
  }

  // Set promiscuous mode:
  if (promiscuous) {
    printf("Doing promiscuous setup\n");
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ethname, sizeof(ifr.ifr_name) - 1);
    int rc = ioctl(fd, SIOCGIFFLAGS, &ifr);
    ASSERT_ERRNO(rc >= 0);
    ifr.ifr_flags |=  IFF_PROMISC;
    rc = ioctl(fd, SIOCSIFFLAGS, &ifr);
    ASSERT_ERRNO(rc >= 0);
  }
  if (multicast) {
    struct packet_mreq mr;
    memset(&mr, 0, sizeof(mr));
    mr.mr_ifindex = ifr.ifr_ifindex;
    if (promiscuous) {
      printf("Doing promiscuous setup\n");
      mr.mr_type = PACKET_MR_PROMISC;
    }
    if (multicast) {
      printf("Doing multicast setup\n");
      mr.mr_type = PACKET_MR_MULTICAST;
      mr.mr_alen = ETH_ALEN;
      unsigned char addr[] = {0x01, 0x80, 0xc2, 0x00, 0x00, 0x03};
      memcpy(&mr.mr_address, addr, sizeof(addr));
    }
    int rc = setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr));
    ASSERT_ERRNO(rc >= 0);
  }
  return fd;
}

void list_interfaces() {
  for (int idx = 1; idx < kMaxIfaceIdx; idx++) {
    int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd < 0) {
      perror("socket");
      return;
    }
    struct ifreq ifr = {0};
    ifr.ifr_ifindex = idx;
    int rc = ioctl(fd, SIOCGIFNAME, &ifr) >= 0;
    int success = rc >= 0;  // should be > 0?
    do {
      rc = close(fd);
    } while (rc < 0 && errno == EINTR);
    if (success) {
      char name[IF_NAMESIZE + 1] = {0};
      strncpy(name, ifr.ifr_name, IF_NAMESIZE);
      if (name[0])
        printf("%s\n", name);
    }
  }
}

void move_packet(int from, int to, const char* fromstr, const char* tostr) {
  unsigned char buf[2048];
  int rc = recv(from, buf, sizeof(buf), 0);
  ASSERT_ERRNO(rc > 0);
  int send_rc = send(to, buf, rc, 0);
  ASSERT_ERRNO(rc == send_rc);
  printf("just sent a packet! fds: %s -> %s\n", fromstr, tostr);
}

int main(int argc, char** argv) {
  if (argc == 2 && !strcmp(argv[1], "-l")) {
    list_interfaces();
    return 0;
  }

  if (argc < 3)
    usage(argv[0]);

  int promiscuous = 1;
  int multicast = 0;
  const char* iface_ont = NULL;
  const char* iface_int = NULL;

  if (argc == 4) {
    if (!strcmp(argv[1], "-m")) {
      promiscuous = 0;
      multicast = 1;
    }
    if (!strcmp(argv[1], "-n")) {
      promiscuous = 0;
      multicast = 0;
    }
    iface_ont = argv[2];
    iface_int = argv[3];
  } else {
    ASSERT(argc == 3);
    iface_ont = argv[1];
    iface_int = argv[2];
  }

  printf("opening iface %s\n", iface_ont);
  int s_ont = open_iface(iface_ont, promiscuous, multicast);
  ASSERT(s_ont >= 0);
  printf("opening iface %s\n", iface_int);
  int s_int = open_iface(iface_int, promiscuous, multicast);
  ASSERT(s_int >= 0);
  int max_fd = MAX(s_ont, s_int);

  // route the packets
  fd_set read_fds;
  while (1) {
    FD_ZERO(&read_fds);
    FD_SET(s_ont, &read_fds);
    FD_SET(s_int, &read_fds);
    int rc = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
    ASSERT_ERRNO(rc >= 0);
    ASSERT(rc > 0);
    if (FD_ISSET(s_ont, &read_fds))
      move_packet(s_ont, s_int, iface_ont, iface_int);
    if (FD_ISSET(s_int, &read_fds))
      move_packet(s_int, s_ont, iface_int, iface_ont);
  }
}
