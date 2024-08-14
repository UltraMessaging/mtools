/* mrcv.c */

#define _GNU_SOURCE  /* Needed for recvmmsg */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#define MAX_UDP_PAYLOAD 1472

/* program options */
int o_linger_ms;
int o_multi_rcv;
int o_num_msgs_expected;
int o_rcvbuf_size;
int o_v_bitmask;
int o_wait_ms;

/* program positional parameters */
unsigned long int groupaddr;
unsigned short int groupport;
char *bind_if;

#define STATE_INIT 0
#define STATE_MEASURING 1
#define STATE_QUITTING 2

/* Globals. */
int quit;
int state;
int num_msgs;
int num_warmups;
int num_quits;
int num_ooo;
uint8_t *sqn_cnt;
uint32_t prev_sqn;
struct timespec start_ts;
struct timespec stop_ts;
struct timespec last_pkt_ts;
int max_dgrams_in_loop;


#define CHKERR(chkerr_s_) do { \
  if ((chkerr_s_) == -1) { \
    fprintf(stderr, "Error %s:%d %s ", __FILE__, (int)__LINE__, #chkerr_s_); \
    perror(NULL); \
    exit(1); \
  } \
} while (0)

#define DIFF_TS(diff_ts_result_ns_, diff_ts_end_ns_, diff_ts_start_ns_) do { \
  (diff_ts_result_ns_) = (((uint64_t)diff_ts_end_ns_.tv_sec \
                           - (uint64_t)diff_ts_start_ns_.tv_sec) * 1000000000ull \
                          + (uint64_t)diff_ts_end_ns_.tv_nsec) \
                         - (uint64_t)diff_ts_start_ns_.tv_nsec; \
} while (0)  /* DIFF_TS */


char usage_str[] = "[-h] [-l linger_ms] [-m multi_rcv] [-n num_msgs_expected] [-r rcvbuf_size] [-v v_bitmask] [-w wait_ms] group port interface";
void usage(char *msg)
{
  fprintf(stderr, "\n%s\n\n", msg);
  fprintf(stderr, "Usage: mdump %s\n\n"
                  "(use -h for detailed help)\n",
          usage_str);
  exit(1);
}  /* usage */

void help()
{
  fprintf(stderr, "Usage: mdump %s\n", usage_str);
  fprintf(stderr, "Where:\n"
          "  -h : help\n"
          "  -l linger_ms : time to delay before exiting\n"
          "  -m multi_rcv : use recvmmsg()\n"
          "  -n num_msgs_expected : messages sent by msnd\n"
          "  -r rcvbuf_size : size (bytes) of UDP receive buffer (SO_RCVBUF)\n"
          "                   (use 0 for system default buff size)\n"
          "  -v v_bitmask : verbosity (1=per msg, 2=sqn issues)\n"
          "  -w wait_ms : timeout for epoll_wait\n"
          "\n"
          "  group : multicast address to receive (required)\n"
          "  port : destination port (required)\n"
          "  interface : IP addr of local interface (for multi-homed hosts) [INADDR_ANY]\n"
  );
}  /* help */


void handle_signal(int signal) {
  quit = 1;
}  /* handle_signal */


void get_parms(int argc, char **argv)
{
  int opt;
  int num_parms;

  /* default values for options */
  o_linger_ms = 100;
  o_multi_rcv = 0;
  o_num_msgs_expected = 0;
  o_rcvbuf_size = 0x800000;  /* 8MB */
  o_v_bitmask = 0;

  /* default values for optional positional params */
  bind_if = NULL;

  while ((opt = getopt(argc, argv, "hl:m:n:r:v:w:")) != EOF) {
    switch (opt) {
    case 'h':
      help();  exit(0);
      break;
    case 'l':
      o_linger_ms = atoi(optarg);
      break;
    case 'm':
      o_multi_rcv = atoi(optarg);
      break;
    case 'n':
      o_num_msgs_expected = atoi(optarg);
      break;
    case 'r':
      o_rcvbuf_size = atoi(optarg);
      break;
    case 'v':
      o_v_bitmask = atoi(optarg);
      break;
    case 'w':
      o_wait_ms = atoi(optarg);
      break;
    default:
      usage("unrecognized option");
      exit(1);
      break;
    }  /* switch */
  }  /* while opt */

  num_parms = argc - optind;

  /* handle positional parameters */
  if (num_parms == 3) {
    groupaddr = inet_addr(argv[optind]);
    groupport = (unsigned short)atoi(argv[optind+1]);
    bind_if  = argv[optind+2];
  } else {
    usage("need 2-3 positional parameters");
    exit(1);
  }
}  /* get_parms */


void process_datagram(uint32_t *buffer, int len)
{
  if (o_v_bitmask & 1) {
    printf("Process datagram, size=%d, type=%d sqn=%10u\n",
           (int)len, buffer[0], buffer[1]);
  }

  if (buffer[0] == 0) {
    num_msgs = 0;
    num_warmups++;
    if (state == STATE_INIT) {
      start_ts = last_pkt_ts;
    }
  }
  else if (buffer[0] == 1) {
    uint32_t sqn = buffer[1];
    if (o_v_bitmask & 2) {
      sqn_cnt[sqn]++;
    }
    if (sqn != prev_sqn + 1) {
      num_ooo++;
    }
    prev_sqn = sqn;
    num_msgs++;
    state = STATE_MEASURING;
  }
  else if (buffer[0] == 2) {
    if (state == STATE_MEASURING) {
      stop_ts = last_pkt_ts;
      state = STATE_QUITTING;
    }
    num_quits++;
  }
  else {
    printf("Unexpected message: 0x%02x, quitting\n", buffer[0]);
    quit = 1;
  }
}  /* process_datagram */


int main(int argc, char **argv)
{
  uint32_t *buff;
  int i;
  int opt;
  struct epoll_event ev, events[100];
  int flags;
  int sockfd;
  int epollfd;
  socklen_t fromlen = sizeof(struct sockaddr_in);
  int cur_size;
  socklen_t opt_sz;
  struct sockaddr_in name;
  struct sockaddr_in src;
  struct ip_mreq imr;
  struct sockaddr_in *client_addrs;
  struct mmsghdr *msgs;
  struct iovec *iovecs;
  int msg_len = 0;
  uint64_t linger_ns;
  uint64_t tot_bits;
  uint64_t tot_ns;
  double msgs_per_sec, bits_per_sec;

  quit = 0;
  state = STATE_INIT;
  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);

  get_parms(argc, argv);

  sqn_cnt = (uint8_t *)malloc(o_num_msgs_expected);
  if (sqn_cnt == NULL) { fprintf(stderr, "Error, %s:%d, malloc failed\n", __FILE__, __LINE__); exit(1); }
  for (i = 0; i < o_num_msgs_expected; i++) {
    sqn_cnt[i] = 0;
  }

  client_addrs = (struct sockaddr_in *)malloc(o_multi_rcv * sizeof(*client_addrs));
  msgs = (struct mmsghdr *)malloc(o_multi_rcv * sizeof(*msgs));
  iovecs = (struct iovec *)malloc(o_multi_rcv * sizeof(*iovecs));
  buff = (uint32_t *)malloc(o_multi_rcv * MAX_UDP_PAYLOAD);
  if (buff == NULL) { fprintf(stderr, "Error, %s:%d, malloc failed\n", __FILE__, __LINE__); exit(1); }

  for (i = 0; i < o_multi_rcv; i++) {
    memset(&client_addrs[i], 0, sizeof(client_addrs[i]));
    iovecs[i].iov_base = &buff[i * (MAX_UDP_PAYLOAD/sizeof(uint32_t))];
    iovecs[i].iov_len = MAX_UDP_PAYLOAD;

    msgs[i].msg_len = 0;
    msgs[i].msg_hdr.msg_name = &client_addrs[i];
    msgs[i].msg_hdr.msg_namelen = sizeof(client_addrs[i]);
    msgs[i].msg_hdr.msg_iov = &iovecs[i];
    msgs[i].msg_hdr.msg_iovlen = 1;
    msgs[i].msg_hdr.msg_control = NULL;
    msgs[i].msg_hdr.msg_controllen = 0;
    msgs[i].msg_hdr.msg_flags = 0;
  }

  CHKERR(epollfd = epoll_create1(0));

  CHKERR(sockfd = socket(PF_INET,SOCK_DGRAM,0));

  /* Make non-blocking. */
  CHKERR(flags = fcntl(sockfd, F_GETFL, 0));
  flags = (flags | O_NONBLOCK);
  CHKERR(fcntl(sockfd, F_SETFL, flags));

  CHKERR(setsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,(const char *)&o_rcvbuf_size, sizeof(o_rcvbuf_size)));

  opt_sz = (socklen_t)sizeof(cur_size);
  CHKERR(getsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,(char *)&cur_size, (socklen_t *)&opt_sz));
  if (cur_size < o_rcvbuf_size) {
    printf("WARNING: tried to set SO_RCVBUF to %d, only got %d\n", o_rcvbuf_size, cur_size); fflush(stdout);
  }

  memset((char *)&imr,0,sizeof(imr));
  imr.imr_multiaddr.s_addr = groupaddr;
  imr.imr_interface.s_addr = inet_addr(bind_if);

  opt = 1;
  CHKERR(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)));

  memset((char *)&name,0,sizeof(name));
  name.sin_family = AF_INET;
  name.sin_addr.s_addr = groupaddr;
  name.sin_port = htons(groupport);
  CHKERR(bind(sockfd,(struct sockaddr *)&name,sizeof(name)));

  CHKERR(setsockopt(sockfd,IPPROTO_IP,IP_ADD_MEMBERSHIP, (char *)&imr,sizeof(struct ip_mreq)));

  /* Register sockfd with epoll. */
  ev.events = EPOLLIN;
  ev.data.fd = sockfd;
  CHKERR(epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ev));

  num_warmups = 0;
  num_quits = 0;
  num_ooo = 0;
  prev_sqn = (uint32_t)-1;
  max_dgrams_in_loop = 1;
  linger_ns = (uint64_t)o_linger_ms * 1000000;

  while (!quit) {
    int nfds, ev;

    CHKERR(nfds = epoll_wait(epollfd, events, 100, o_wait_ms));

    if (nfds == 0) {
      if (state != STATE_INIT) {
        /* Nothing received (timeout). If it's been a while, quit. */
        struct timespec timeout_ts;
        int64_t ns_since_last_pkt;
        clock_gettime(CLOCK_MONOTONIC, &timeout_ts);
        DIFF_TS(ns_since_last_pkt, timeout_ts, last_pkt_ts);
        if (ns_since_last_pkt > linger_ns) {
          quit = 1;
        }
      }
    } else {  /* nfds > 0 */
      clock_gettime(CLOCK_MONOTONIC, &last_pkt_ts);
    }

    for (ev = 0; ev < nfds; ++ev) {
      if (events[ev].events & EPOLLIN) {

        if (o_multi_rcv == 0) {  /* Single receive. */
          CHKERR(cur_size = recvfrom(events[ev].data.fd, buff, MAX_UDP_PAYLOAD, 0, (struct sockaddr *)&src, &fromlen));
          if (msg_len == 0) {
            msg_len = cur_size;
          }
          else if (cur_size != msg_len) {
            fprintf(stderr, "ERROR, cur_size=%d, msg_len=%d\n", cur_size, msg_len);
            exit(1);
          }
          process_datagram(buff, cur_size);
        }  /* single read */

        else {  /* multi-receive */
          int n_dgrams;
          CHKERR(n_dgrams = recvmmsg(events[ev].data.fd, msgs, o_multi_rcv, 0, NULL));
          if (n_dgrams == 0) { printf("recvmmsg(%d) returned 0\n", events[ev].data.fd); }
          if (n_dgrams > max_dgrams_in_loop) {
            max_dgrams_in_loop = n_dgrams;
          }

          uint32_t *b = buff;
          for (i = 0; i < n_dgrams; ++i) {
            cur_size = msgs[i].msg_len;
            if (msg_len == 0) {
              msg_len = cur_size;
            }
            else if (cur_size != msg_len) {
              fprintf(stderr, "ERROR, cur_size=%d, msg_len=%d\n", cur_size, msg_len);
              exit(1);
            }
            process_datagram(b, cur_size);

            b += (MAX_UDP_PAYLOAD/sizeof(uint32_t));  /* Step to the next buffer. */
          }  /* for i */
        }  /* multi-read */
      }  /* if EPOLLIN */
      else {
        printf("Warning, events[%d].events = 0x%x, .data.fd=%d\n",
            ev, events[ev].events, events[ev].data.fd);
      }
    }
  }  /* while !quit */

  if (state == STATE_MEASURING) {
    stop_ts = last_pkt_ts;
  }

  DIFF_TS(tot_ns, stop_ts, start_ts);

  tot_bits = (uint64_t)num_msgs * (uint64_t)8 * (
      (uint64_t)msg_len  /* UDP payload */
      + (uint64_t)8  /* UDP header */
      + (uint64_t)20  /* IP header */
      + (uint64_t)14  /* Ethernet header */
      + (uint64_t)4   /* Ethernet FSC */
      + (uint64_t)12  /* Interframe gap (96 bits) */
  );

  msgs_per_sec = (double)num_msgs;
  msgs_per_sec /= (double)tot_ns;
  msgs_per_sec *= 1000000000.0;

  bits_per_sec = (double)tot_bits;
  bits_per_sec /= (double)tot_ns;
  bits_per_sec *= 1000000000.0;

  printf("\n");

  if (o_v_bitmask & 2) {
    printf("Sqn issues: ");
    for (i = 0; i < o_num_msgs_expected; i++) {
      if (sqn_cnt[i] != 1) {
        printf("sqn_cnt[%d]=%d ", i, sqn_cnt[i]);
      }
    }
  }

  printf("\n");
  printf("o_linger_ms=%d, o_multi_rcv=%d, o_num_msgs_expected=%d, o_rcvbuf_size=%d, o_v_bitmask=%d\n",
          o_linger_ms, o_multi_rcv, o_num_msgs_expected, o_rcvbuf_size, o_v_bitmask);
  printf("%d dgrams at %.0f dgrams/sec (%.0f bits/sec), %d max dgrams in loop, %d warmups, %d quits, %d ooo, %d loss (%.2f%%)\n",
         num_msgs, msgs_per_sec, bits_per_sec, max_dgrams_in_loop, num_warmups, num_quits, num_ooo,
         o_num_msgs_expected - (int)num_msgs,
         ((double)o_num_msgs_expected - (double)num_msgs) * 100.0 / (double)o_num_msgs_expected);

  close(sockfd);
  close(epollfd);
  free(buff);

  return 0;
}  /* main */
