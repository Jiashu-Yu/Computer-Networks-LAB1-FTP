#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>     /* defines STDIN_FILENO, system calls,etc */
#include <sys/types.h>  /* system data type definitions */
#include <sys/socket.h> /* socket specific definitions */
#include <netinet/in.h> /* INET constants and stuff */
#include <arpa/inet.h>  /* IP address conversion stuff */
#include <netdb.h>      /* gethostbyname */

#define MAXBUF 1024*1024    // 1MB

void uppercase(char *p) {
	for ( ; *p; ++p) *p = toupper(*p);
}

void echo(int sd) {
		char bufin[MAXBUF];
		struct sockaddr_in remote;
		int seq_num = 0;
		char response[MAXBUF];

		/* need to know how big address struct is, len must be set before the
			 call to recvfrom!!! */
		socklen_t len = sizeof(remote);

		while (1) {
			/* read a datagram from the socket (put result in bufin) */
			int n = recvfrom(sd, bufin, MAXBUF, 0, (struct sockaddr *) &remote, &len);    // n是收到的字节数

			if (n < 0) {
				perror("Error receiving data");
			} else {
				bufin[n] = '\0';
				// uppercase(bufin);
				size_t response_len = snprintf(response, sizeof(response), "%d %s", seq_num, bufin);
				seq_num += 1;

				/* Got something, just send it back */
				sendto(sd, response, response_len, 0, (struct sockaddr *)&remote, len);
			}
		}
}

/* server main routine */

int main() {
	int ld;     // listening descriptor (用来存储socket的文件描述符)
	struct sockaddr_in skaddr;    //存放socket的IP地址、端口号等
	socklen_t length;     //存储地址struct的长度

	/* create a socket
		 IP protocol family (PF_INET--IPv4)
		 UDP protocol (SOCK_DGRAM)
	*/

	if ((ld = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("Problem creating socket\n");
		exit(1);
	}

	/* establish our address
		 address family is AF_INET(IPv4)
		 our IP address is INADDR_ANY (any of our IP addresses)
		 the port number is 9876
	*/

	skaddr.sin_family = AF_INET;
	skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	skaddr.sin_port = htons(9876);

	if (bind(ld, (struct sockaddr *) &skaddr, sizeof(skaddr)) < 0) {
		printf("Problem binding\n");
		exit(0);
	}

	/* find out what port we were assigned and print it out */

	length = sizeof(skaddr);
	if (getsockname(ld, (struct sockaddr *) &skaddr, &length) < 0) {
		printf("Error getsockname\n");
		exit(1);
	}

	/* Go echo every datagram we get */
	echo(ld);
	return 0;
}
