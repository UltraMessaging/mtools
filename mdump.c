/* mdump.c */
/*   Program to dump the contents of all datagrams arriving on a specified
 * multicast address and port.  The dump gives both the hex and ASCII
 * equivalents of the datagram payload.
 * See https://github.com/UltraMessaging/mtools
 *
 * Author: J.P.Knight@lut.ac.uk (heavily modified by 29West/Informatica)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted without restriction.
 *
 * Note: this program is based on the sd_listen program by Tom Pusateri
 * (pusateri@cs.duke.edu) and developed by Jon Knight (J.P.Knight@lut.ac.uk).
 *
  THE SOFTWARE IS PROVIDED "AS IS" AND INFORMATICA DISCLAIMS ALL WARRANTIES
  EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY IMPLIED WARRANTIES OF
  NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR
  PURPOSE.  INFORMATICA DOES NOT WARRANT THAT USE OF THE SOFTWARE WILL BE
  UNINTERRUPTED OR ERROR-FREE.  INFORMATICA SHALL NOT, UNDER ANY CIRCUMSTANCES,
  BE LIABLE TO LICENSEE FOR LOST PROFITS, CONSEQUENTIAL, INCIDENTAL, SPECIAL OR
  INDIRECT DAMAGES ARISING OUT OF OR RELATED TO THIS AGREEMENT OR THE
  TRANSACTIONS CONTEMPLATED HEREUNDER, EVEN IF INFORMATICA HAS BEEN APPRISED OF
  THE LIKELIHOOD OF SUCH DAMAGES.
 */

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define snprintf _snprintf
#endif

/* Many of the following definitions are intended to make it easier to write
 * portable code between windows and unix. */

/* use our own form of getopt */
extern int toptind;
extern int toptreset;
extern char *toptarg;
int tgetopt(int nargc, char * const *nargv, const char *ostr);

#if defined(_MSC_VER)
/* Windows-only includes */
#include <winsock2.h>
typedef unsigned long socklen_t;
#define SLEEP_SEC(s) Sleep((s) * 1000)
#define SLEEP_MSEC(s) Sleep(s)
#define ERRNO GetLastError()
#define CLOSESOCKET closesocket
#define TLONGLONG signed __int64

#else
/* Unix-only includes */
#define HAVE_PTHREAD_H
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#define SLEEP_SEC(s) sleep(s)
#define SLEEP_MSEC(s) usleep((s) * 1000)
#define CLOSESOCKET close
#define ERRNO errno
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define TLONGLONG signed long long
#endif

#if defined(_WIN32)
#   include <ws2tcpip.h>
#   include <sys\types.h>
#   include <sys\timeb.h>
#   define perror(x) fprintf(stderr,"%s: %d\n",x,GetLastError())
#else
#   include <sys/time.h>
#endif

#include <string.h>
#include <time.h>

#define MAXPDU 65536


/* program name (from argv[0] */
char *prog_name = "xxx";

/* program options */
int o_quiet_lvl;
int o_rcvbuf_size;
int o_pause_ms;
int o_pause_num;
int o_verify;
int o_stop;
int o_tcp;
FILE *o_output;
char o_output_equiv_opt[1024];

/* program positional parameters */
unsigned long int groupaddr;
unsigned short int groupport;
char *bind_if;


char usage_str[] = "[-h] [-o ofile] [-p pause_ms[/loops]] [-Q Quiet_lvl] [-q] [-r rcvbuf_size] [-s] [-t] [-v] group port [interface]";

void usage(char *msg)
{
	if (msg != NULL)
		fprintf(stderr, "\n%s\n\n", msg);
	fprintf(stderr, "Usage: %s %s\n\n"
			"(use -h for detailed help)\n",
			prog_name, usage_str);
}  /* usage */


void help(char *msg)
{
	if (msg != NULL)
		fprintf(stderr, "\n%s\n\n", msg);
	fprintf(stderr, "Usage: %s %s\n", prog_name, usage_str);
	fprintf(stderr, "Where:\n"
			"  -h : help\n"
			"  -o ofile : print results to file (in addition to stdout)\n"
			"  -p pause_ms[/num] : milliseconds to pause after each receive [0: no pause]\n"
			"                      and number of loops to apply the pause [0: all loops]\n"
			"  -Q Quiet_lvl : set quiet level [0] :\n"
			"                 0 - print full datagram contents\n"
			"                 1 - print datagram summaries\n"
			"                 2 - no print per datagram (same as '-q')\n"
			"  -q : no print per datagram (same as '-Q 2')\n"
			"  -r rcvbuf_size : size (bytes) of UDP receive buffer (SO_RCVBUF) [4194304]\n"
			"                   (use 0 for system default buff size)\n"
			"  -s : stop execution when status msg received\n"
			"  -t : Use TCP (use '0.0.0.0' for group)\n"
			"  -v : verify the sequence numbers\n"
			"\n"
			"  group : multicast address to receive (required, use '0.0.0.0' for unicast)\n"
			"  port : destination port (required)\n"
			"  interface : optional IP addr of local interface (for multi-homed hosts) [INADDR_ANY]\n"
	);
}  /* help */


/* faster routine to replace inet_ntoa() (from tcpdump) */
char *intoa(unsigned int addr)
{
	register char *cp;
	register unsigned int byte;
	register int n;
	static char buf[sizeof(".xxx.xxx.xxx.xxx")];

	addr = ntohl(addr);
	// NTOHL(addr);
	cp = &buf[sizeof buf];
	*--cp = '\0';

	n = 4;
	do {
		byte = addr & 0xff;
		*--cp = byte % 10 + '0';
		byte /= 10;
		if (byte > 0) {
			*--cp = byte % 10 + '0';
			byte /= 10;
			if (byte > 0)
				*--cp = byte + '0';
		}
		*--cp = '.';
		addr >>= 8;
	} while (--n > 0);

	return cp + 1;
}  /* intoa */


/* Return ptr to ascii time string. NOT THREAD SAFE! */
char *format_time(const struct timeval *in_tv)
{
	/* Static so that a pointer to it can be returned. */
	static char buff[sizeof(".xx:xx:xx.xxxxxx")];

	time_t epoch_sec = (time_t)in_tv->tv_sec;
	struct tm *in_tm = localtime(&epoch_sec);

	snprintf(buff, sizeof(buff), "%02d:%02d:%02d.%06d",
		in_tm->tm_hour, in_tm->tm_min, in_tm->tm_sec, (int)in_tv->tv_usec);
	return buff;
}  /* format_time */


void dump(FILE *ofile, const char *buffer, int size)
{
	int i,j;
	unsigned char c;
	char textver[20];

	for (i=0;i<(size >> 4);i++) {
		for (j=0;j<16;j++) {
			c = buffer[(i << 4)+j];
			fprintf(ofile, "%02x ",c);
			textver[j] = ((c<0x20)||(c>0x7e))?'.':c;
		}
		textver[j] = 0;
		fprintf(ofile, "\t%s\n",textver);
	}
	for (i=0;i<size%16;i++) {
		c = buffer[size-size%16+i];
		fprintf(ofile, "%02x ",c);
		textver[i] = ((c<0x20)||(c>0x7e))?'.':c;
	}
	for (i=size%16;i<16;i++) {
		fprintf(ofile, "   ");
		textver[i] = ' ';
	}
	textver[i] = 0;
	fprintf(ofile, "\t%s\n",textver); fflush(ofile);
}  /* dump */


void currenttv(struct timeval *tv)
{
#if defined(_WIN32)
	struct _timeb tb;
	_ftime(&tb);
	tv->tv_sec = tb.time;
	tv->tv_usec = 1000*tb.millitm;
#else
	gettimeofday(tv,NULL);
#endif /* _WIN32 */
}  /* currenttv */


int main(int argc, char **argv)
{
	int opt;
	int num_parms;
	char equiv_cmd[1024];
	char *buff;
	SOCKET listensock;
	SOCKET sock;
	socklen_t fromlen = sizeof(struct sockaddr_in);
	int default_rcvbuf_sz, cur_size, sz;
	int num_rcvd;
	struct sockaddr_in name;
	struct sockaddr_in src;
	struct ip_mreq imr;
	struct timeval tv;
	int num_sent;
	float perc_loss;
	int cur_seq;
	char *pause_slash;

	prog_name = argv[0];

	buff = malloc(65536 + 1);  /* one extra for trailing null (if needed) */
	if (buff == NULL) { fprintf(stderr, "malloc failed\n"); exit(1); }

#if defined(_WIN32)
	{
		WSADATA wsadata;  int wsstatus;
		if ((wsstatus = WSAStartup(MAKEWORD(2,2), &wsadata)) != 0) {
			fprintf(stderr,"%s: WSA startup error - %d\n", argv[0], wsstatus);
			exit(1);
		}
	}
#else
	signal(SIGPIPE, SIG_IGN);
#endif /* _WIN32 */

	/* get system default value for socket buffer size */
	if((sock = socket(PF_INET,SOCK_DGRAM,0)) == INVALID_SOCKET) {
		fprintf(stderr, "ERROR: ");  perror("socket");
		exit(1);
	}
	sz = sizeof(default_rcvbuf_sz);
	if (getsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char *)&default_rcvbuf_sz,
			(socklen_t *)&sz) == SOCKET_ERROR) {
		fprintf(stderr, "ERROR: ");  perror("getsockopt - SO_RCVBUF");
		exit(1);
	}
	CLOSESOCKET(sock);

	/* default values for options */
	o_quiet_lvl = 0;
	o_rcvbuf_size = 0x400000;  /* 4MB */
	o_pause_ms = 0;
	o_pause_num = 0;
	o_verify = 0;
	o_stop = 0;
	o_tcp = 0;
	o_output = NULL;
	o_output_equiv_opt[0] = '\0';

	/* default values for optional positional params */
	bind_if = NULL;

	while ((opt = tgetopt(argc, argv, "hqQ:p:r:o:vst")) != EOF) {
		switch (opt) {
		  case 'h':
			help(NULL);  exit(0);
			break;
		  case 'q':
			o_quiet_lvl = 2;
			break;
		  case 'Q':
			o_quiet_lvl = atoi(toptarg);
			break;
		  case 'p':
			pause_slash = strchr(toptarg, '/');
			if (pause_slash)
				o_pause_num = atoi(pause_slash+1);
			o_pause_ms = atoi(toptarg);
			break;
		  case 'r':
			o_rcvbuf_size = atoi(toptarg);
			if (o_rcvbuf_size == 0)
				o_rcvbuf_size = default_rcvbuf_sz;
			break;
		  case 'v':
			o_verify = 1;
			break;
		  case 's':
			o_stop = 1;
			break;
		  case 't':
			o_tcp = 1;
			break;
		  case 'o':
			if (strlen(toptarg) > 1000) {
				fprintf(stderr, "ERROR: file name too long (%s)\n", toptarg);
				exit(1);
			}
			o_output = fopen(toptarg, "w");
			if (o_output == NULL) {
				fprintf(stderr, "ERROR: ");  perror("fopen");
				exit(1);
			}
			snprintf(o_output_equiv_opt, sizeof(o_output_equiv_opt), "-o %s ", toptarg);
			break;
		  default:
			usage("unrecognized option");
			exit(1);
			break;
		}  /* switch */
	}  /* while opt */

	num_parms = argc - toptind;

	/* handle positional parameters */
	if (num_parms == 2) {
		groupaddr = inet_addr(argv[toptind]);
		groupport = (unsigned short)atoi(argv[toptind+1]);
		snprintf(equiv_cmd, sizeof(equiv_cmd), "mdump %s-p%d -Q%d -r%d %s%s%s%s %s",
				o_output_equiv_opt, o_pause_ms, o_quiet_lvl, o_rcvbuf_size,
				o_stop ? "-s " : "",
				o_tcp ? "-t " : "",
				o_verify ? "-v " : "",
				argv[toptind],argv[toptind+1]);
		printf("Equiv cmd line: %s\n", equiv_cmd); fflush(stdout);
		if (o_output) { fprintf(o_output, "Equiv cmd line: %s\n", equiv_cmd); fflush(o_output); }
	} else if (num_parms == 3) {
		groupaddr = inet_addr(argv[toptind]);
		groupport = (unsigned short)atoi(argv[toptind+1]);
		bind_if  = argv[toptind+2];
		snprintf(equiv_cmd, sizeof(equiv_cmd), "mdump %s-p%d -Q%d -r%d %s%s%s%s %s %s",
				o_output_equiv_opt, o_pause_ms, o_quiet_lvl, o_rcvbuf_size,
				o_stop ? "-s " : "",
				o_tcp ? "-t " : "",
				o_verify ? "-v " : "",
				argv[toptind],argv[toptind+1],argv[toptind+2]);
		printf("Equiv cmd line: %s\n", equiv_cmd); fflush(stdout);
		if (o_output) { fprintf(o_output, "Equiv cmd line: %s\n", equiv_cmd); fflush(o_output); }
	} else {
		usage("need 2-3 positional parameters");
		exit(1);
	}

	if (o_tcp && groupaddr != inet_addr("0.0.0.0")) {
		usage("-t incompatible with non-zero multicast group");
	}

	if (o_tcp) {
		if((listensock = socket(PF_INET,SOCK_STREAM,0)) == INVALID_SOCKET) {
			fprintf(stderr, "ERROR: ");  perror("socket");
			exit(1);
		}
		memset((char *)&name,0,sizeof(name));
		name.sin_family = AF_INET;
		name.sin_addr.s_addr = groupaddr;
		name.sin_port = htons(groupport);
		if (bind(listensock,(struct sockaddr *)&name,sizeof(name)) == SOCKET_ERROR) {
			fprintf(stderr, "ERROR: ");  perror("bind");
			exit(1);
		}
		if(listen(listensock, 1) == SOCKET_ERROR) {
			fprintf(stderr, "ERROR: ");  perror("listen");
			exit(1);
		}
		if((sock = accept(listensock,(struct sockaddr *)&src,&fromlen)) == INVALID_SOCKET) {
			fprintf(stderr, "ERROR: ");  perror("accept");
			exit(1);
		}
	} else {
		if((sock = socket(PF_INET,SOCK_DGRAM,0)) == INVALID_SOCKET) {
			fprintf(stderr, "ERROR: ");  perror("socket");
			exit(1);
		}
	}

	if(setsockopt(sock,SOL_SOCKET,SO_RCVBUF,(const char *)&o_rcvbuf_size,
			sizeof(o_rcvbuf_size)) == SOCKET_ERROR) {
		printf("WARNING: setsockopt - SO_RCVBUF\n"); fflush(stdout);
		if (o_output) { fprintf(o_output, "WARNING: "); perror("setsockopt - SO_RCVBUF"); fflush(o_output); }
	}
	sz = sizeof(cur_size);
	if (getsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char *)&cur_size,
			(socklen_t *)&sz) == SOCKET_ERROR) {
		fprintf(stderr, "ERROR: ");  perror("getsockopt - SO_RCVBUF");
		exit(1);
	}
	if (cur_size < o_rcvbuf_size) {
		printf("WARNING: tried to set SO_RCVBUF to %d, only got %d\n", o_rcvbuf_size, cur_size); fflush(stdout);
		if (o_output) { fprintf(o_output, "WARNING: tried to set SO_RCVBUF to %d, only got %d\n", o_rcvbuf_size, cur_size); fflush(o_output); }
	}

	if (groupaddr != inet_addr("0.0.0.0")) {
		memset((char *)&imr,0,sizeof(imr));
		imr.imr_multiaddr.s_addr = groupaddr;
		if (bind_if != NULL) {
			imr.imr_interface.s_addr = inet_addr(bind_if);
		} else {
			imr.imr_interface.s_addr = htonl(INADDR_ANY);
		}
	}

	opt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) == SOCKET_ERROR) {
		fprintf(stderr, "ERROR: ");  perror("setsockopt SO_REUSEADDR");
		exit(1);
	}

	if (! o_tcp) {
		memset((char *)&name,0,sizeof(name));
		name.sin_family = AF_INET;
		name.sin_addr.s_addr = groupaddr;
		name.sin_port = htons(groupport);
		if (bind(sock,(struct sockaddr *)&name,sizeof(name)) == SOCKET_ERROR) {
			/* So OSes don't want you to bind to the m/c group. */
			name.sin_addr.s_addr = htonl(INADDR_ANY);
			if (bind(sock,(struct sockaddr *)&name, sizeof(name)) == SOCKET_ERROR) {
				fprintf(stderr, "ERROR: ");  perror("bind");
				exit(1);
			}
		}

		if (groupaddr != inet_addr("0.0.0.0")) {
			if (setsockopt(sock,IPPROTO_IP,IP_ADD_MEMBERSHIP,
						(char *)&imr,sizeof(struct ip_mreq)) == SOCKET_ERROR ) {
				fprintf(stderr, "ERROR: ");  perror("setsockopt - IP_ADD_MEMBERSHIP");
				exit(1);
			}
		}
	}

	cur_seq = 0;
	num_rcvd = 0;
	for (;;) {
		if (o_tcp) {
			cur_size = recv(sock,buff,65536,0);
			if (cur_size == 0) {
				printf("EOF\n");
				if (o_output) { fprintf(o_output, "EOF\n"); }
				break;
			}
		} else {
			cur_size = recvfrom(sock,buff,65536,0,
					(struct sockaddr *)&src,&fromlen);
		}
		if (cur_size == SOCKET_ERROR) {
			fprintf(stderr, "ERROR: ");  perror("recv");
			exit(1);
		}

		if (o_quiet_lvl == 0) {  /* non-quiet: print full dump */
			currenttv(&tv);
			printf("%s %s.%d %d bytes:\n",
					format_time(&tv), inet_ntoa(src.sin_addr),
					ntohs(src.sin_port), cur_size);
			dump(stdout, buff,cur_size);
			if (o_output) {
				fprintf(o_output, "%s %s.%d %d bytes:\n",
						format_time(&tv), inet_ntoa(src.sin_addr),
						ntohs(src.sin_port), cur_size);
				dump(o_output, buff,cur_size);
			}
		}
		if (o_quiet_lvl == 1) {  /* semi-quiet: print datagram summary */
			currenttv(&tv);
			printf("%s %s.%d %d bytes\n",  /* no colon */
					format_time(&tv), inet_ntoa(src.sin_addr),
					ntohs(src.sin_port), cur_size);
			fflush(stdout);
			if (o_output) {
				fprintf(o_output, "%s %s.%d %d bytes\n",  /* no colon */
						format_time(&tv), inet_ntoa(src.sin_addr),
						ntohs(src.sin_port), cur_size);
				fflush(o_output);
			}
		}

		if (cur_size > 5 && memcmp(buff, "echo ", 5) == 0) {
			/* echo command */
			buff[cur_size] = '\0';  /* guarantee trailing null */
			if (buff[cur_size - 1] == '\n')
				buff[cur_size - 1] = '\0';  /* strip trailing nl */
			printf("%s\n", buff); fflush(stdout);
			if (o_output) { fprintf(o_output, "%s\n", buff); fflush(o_output); }

			/* reset stats */
			num_rcvd = 0;
			cur_seq = 0;
		}
		else if (cur_size > 5 && memcmp(buff, "stat ", 5) == 0) {
			/* when sender tells us to, calc and print stats */
			buff[cur_size] = '\0';  /* guarantee trailing null */
			/* 'stat' message contains num msgs sent */
			num_sent = atoi(&buff[5]);
			perc_loss = (float)(num_sent - num_rcvd) * 100.0 / (float)num_sent;
			printf("%d msgs sent, %d received (not including 'stat')\n", num_sent, num_rcvd);
			printf("%f%% loss\n", perc_loss);
			fflush(stdout);
			if (o_output) {
				fprintf(o_output, "%d msgs sent, %d received (not including 'stat')\n", num_sent, num_rcvd);
				fprintf(o_output, "%f%% loss\n", perc_loss);
				fflush(o_output);
			}

			if (o_stop)
				exit(0);

			/* reset stats */
			num_rcvd = 0;
			cur_seq = 0;
		}
		else {  /* not a cmd */
			if (o_pause_ms > 0 && ( (o_pause_num > 0 && num_rcvd < o_pause_num)
									|| (o_pause_num == 0) )) {
				SLEEP_MSEC(o_pause_ms);
			}

			if (o_verify) {
				buff[cur_size] = '\0';  /* guarantee trailing null */
				if (cur_seq != strtol(&buff[8], NULL, 16)) {
					printf("Expected seq %x (hex), got %s\n", cur_seq, &buff[8]);
					fflush(stdout);
					/* resyncronize sequence numbers in case there is loss */
					cur_seq = strtol(&buff[8], NULL, 16);
				}
			}

			++num_rcvd;
			++cur_seq;
		}
	}  /* for ;; */

	CLOSESOCKET(sock);
	if (o_tcp)
		CLOSESOCKET(listensock);

	exit(0);
}  /* main */



/* tgetopt.c - (renamed from BSD getopt) - this source was adapted from BSD
 *
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef _BSD
extern char *__progname;
#else
#define __progname "tgetopt"
#endif

int	topterr = 1,		/* if error message should be printed */
	toptind = 1,		/* index into parent argv vector */
	toptopt,			/* character checked for validity */
	toptreset;		/* reset getopt */
char	*toptarg;		/* argument associated with option */

#define	BADCH	(int)'?'
#define	BADARG	(int)':'
#define	EMSG	""

/*
 * tgetopt --
 *	Parse argc/argv argument vector.
 */
int
tgetopt(nargc, nargv, ostr)
	int nargc;
	char * const *nargv;
	const char *ostr;
{
	static char *place = EMSG;		/* option letter processing */
	char *oli;				/* option letter list index */

	/* really reset */
	if (toptreset) {
		topterr = 1;
		toptind = 1;
		toptopt = 0;
		toptreset = 0;
		toptarg = NULL;
		place = EMSG;
	}
	if (!*place) {		/* update scanning pointer */
		if (toptind >= nargc || *(place = nargv[toptind]) != '-') {
			place = EMSG;
			return (-1);
		}
		if (place[1] && *++place == '-') {	/* found "--" */
			++toptind;
			place = EMSG;
			return (-1);
		}
	}					/* option letter okay? */
	if ((toptopt = (int)*place++) == (int)':' ||
	    !(oli = strchr(ostr, toptopt))) {
		/*
		 * if the user didn't specify '-' as an option,
		 * assume it means -1.
		 */
		if (toptopt == (int)'-')
			return (-1);
		if (!*place)
			++toptind;
		if (topterr && *ostr != ':')
			(void)fprintf(stderr,
			    "%s: illegal option -- %c\n", __progname, toptopt);
		return (BADCH);
	}
	if (*++oli != ':') {			/* don't need argument */
		toptarg = NULL;
		if (!*place)
			++toptind;
	}
	else {					/* need an argument */
		if (*place)			/* no white space */
			toptarg = place;
		else if (nargc <= ++toptind) {	/* no arg */
			place = EMSG;
			if (*ostr == ':')
				return (BADARG);
			if (topterr)
				(void)fprintf(stderr,
				    "%s: option requires an argument -- %c\n",
				    __progname, toptopt);
			return (BADCH);
		}
	 	else				/* white space */
			toptarg = nargv[toptind];
		place = EMSG;
		++toptind;
	}
	return (toptopt);			/* dump back option letter */
}  /* tgetopt */
