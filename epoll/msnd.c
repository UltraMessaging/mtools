/* msnd.c */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#define MAX_UDP_PAYLOAD 1472  /* Even multiple of 64. */
#define WARMUP_LOOPS 100
#define END_LOOPS 300

/* program options */
int o_msg_len;
int o_num_msgs;
int o_rate;
int o_sndbuf_size;

/* program positional parameters */
unsigned long int groupaddr;
unsigned short int groupport;
char *bind_if;


struct sockaddr_in group_sin;
int global_max_tight_sends;
uint64_t start_usec;


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


char usage_str[] = "[-h] [-m msg_len] [-n num_msg] [-r rate] [-s sndbuf_size] group port interface";
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
          "  -m msg_len : size (bytes) of UDP datagram\n"
          "  -n num_msg : number of measurement messages to send\n"
          "  -r rate : messages per second to send\n"
          "  -s sndbuf_size : sender socket buffer size\n"
          "\n"
          "  group : multicast address to receive (required)\n"
          "  port : destination port (required)\n"
          "  interface : IP addr of local interface (for multi-homed hosts) [INADDR_ANY]\n"
  );
}  /* help */


void get_parms(int argc, char **argv)
{
  int opt;
  int num_parms;

  /* default values for options */
  o_msg_len = 700 + 32;  /* user data + UM overhead */
  o_num_msgs = 1000000;
  o_rate = 1000;
  o_sndbuf_size = 0;

  /* default values for optional positional params */
  bind_if = NULL;

  while ((opt = getopt(argc, argv, "hm:n:r:s:")) != EOF) {
    switch (opt) {
    case 'h':
      help();  exit(0);
      break;
    case 'm':
      o_msg_len = atoi(optarg);
      if (o_msg_len > MAX_UDP_PAYLOAD) { fprintf(stderr, "msg_len must be 1..%d\n", MAX_UDP_PAYLOAD); exit(1); }
      break;
    case 'n':
      o_num_msgs = atoi(optarg);
      break;
    case 'r':
      o_rate = atoi(optarg);
      break;
    case 's':
      o_sndbuf_size = atoi(optarg);
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


void send_loop(int sockfd, int num_sends, uint64_t sends_per_sec, uint32_t *buffer)
{
  struct timespec cur_ts;
  struct timespec start_ts;
  uint64_t num_sent;
  int max_tight_sends;

  max_tight_sends = 0;

  /* Send messages evenly-spaced using busy looping. Based on algorithm:
   * http://www.geeky-boy.com/catchup/html/ */
  clock_gettime(CLOCK_MONOTONIC, &start_ts);
  cur_ts = start_ts;
  num_sent = 0;
  do {  /* while num_sent < num_sends */
    uint64_t ns_so_far;
    DIFF_TS(ns_so_far, cur_ts, start_ts);
    /* The +1 is because we want to send, then pause. */
    uint64_t should_have_sent = (ns_so_far * sends_per_sec)/1000000000 + 1;
    if (should_have_sent > num_sends) {
      should_have_sent = num_sends;
    }
    if (should_have_sent - num_sent > max_tight_sends) {
      max_tight_sends = should_have_sent - num_sent;
    }

    /* If we are behind where we should be, get caught up. */
    while (num_sent < should_have_sent) {
      /* Send message. */
      CHKERR(sendto(sockfd, buffer, o_msg_len, 0, (struct sockaddr *)&group_sin, sizeof(group_sin)));
      buffer[1]++;

      num_sent++;
    }  /* while num_sent < should_have_sent */
    clock_gettime(CLOCK_MONOTONIC, &cur_ts);
  } while (num_sent < num_sends);

  global_max_tight_sends = max_tight_sends;
}  /* send_loop */


int main(int argc, char **argv)
{
  int opt, i;
  uint32_t buffer[MAX_UDP_PAYLOAD/sizeof(uint32_t)];
  int sockfd;
  struct in_addr iface_in;
  int cur_size, sz;
  uint64_t tot_bits;
  struct timespec start_ts;
  struct timespec stop_ts;
  uint64_t tot_ns;
  double msgs_per_sec, bits_per_sec;

  get_parms(argc, argv);

  CHKERR(sockfd = socket(PF_INET,SOCK_DGRAM,0));

  if (o_sndbuf_size > 0) {
    CHKERR(setsockopt(sockfd,SOL_SOCKET,SO_SNDBUF,(const char *)&o_sndbuf_size, sizeof(o_sndbuf_size)));

    sz = sizeof(cur_size);
    CHKERR(getsockopt(sockfd,SOL_SOCKET,SO_SNDBUF,(char *)&cur_size, (socklen_t *)&sz));
    if (cur_size < o_sndbuf_size) {
      printf("WARNING: tried to set SO_RCVBUF to %d, only got %d\n", o_sndbuf_size, cur_size); fflush(stdout);
    }
  }

  opt = 1;
  CHKERR(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)));

  memset((char *)&iface_in,0,sizeof(iface_in));
  iface_in.s_addr = inet_addr(bind_if);
  CHKERR(setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&iface_in, sizeof(iface_in)));

  memset((char *)&group_sin, 0, sizeof(group_sin));
  group_sin.sin_family = AF_INET;
  group_sin.sin_addr.s_addr = groupaddr;
  group_sin.sin_port = htons(groupport);

  buffer[0] = 0;
  buffer[1] = 0;
  for (i = 0; i < WARMUP_LOOPS; ++i) {
    usleep(1000);  /* 1 ms */
    CHKERR(sendto(sockfd, buffer, o_msg_len, 0, (struct sockaddr *)&group_sin, sizeof(group_sin)));
    buffer[1]++;
  }  /* for ;; */

  buffer[0] = 1;
  buffer[1] = 0;

  clock_gettime(CLOCK_MONOTONIC, &start_ts);
  send_loop(sockfd, o_num_msgs, (uint64_t)o_rate, buffer);
  clock_gettime(CLOCK_MONOTONIC, &stop_ts);
  DIFF_TS(tot_ns, stop_ts, start_ts);

  buffer[0] = 2;
  buffer[1] = 0;

  send_loop(sockfd, END_LOOPS, (uint64_t)o_rate, buffer);

  close(sockfd);

  tot_bits = (uint64_t)o_num_msgs * (uint64_t)8 * (
      (uint64_t)o_msg_len  /* UDP payload */
      +  (uint64_t)8  /* UDP */
      + (uint64_t)20  /* IP */
      + (uint64_t)14  /* Ethernet */
      + (uint64_t)12  /* interpacket gap */
  );

  msgs_per_sec = (double)o_num_msgs;
  msgs_per_sec /= (double)tot_ns;
  msgs_per_sec *= 1000000000.0;

  bits_per_sec = (double)tot_bits;
  bits_per_sec /= (double)tot_ns;
  bits_per_sec *= 1000000000.0;

  printf("o_msg_len=%d, o_num_msgs=%d, o_rate=%d, o_sndbuf_size=%d\n",
         o_msg_len, o_num_msgs, o_rate, o_sndbuf_size);
  printf("%d dgrams at %.0f dgrams/sec (%.0f bits/sec), %d max tight sends\n",
         o_num_msgs, msgs_per_sec, bits_per_sec, global_max_tight_sends);

  return 0;
}  /* main */
