#include <stdio.h>
#include <stdlib.h>                   //for exit()
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>                   //for strerror()
#include <arpa/inet.h>				  //for inet_ntop()
#include <unistd.h>					  //for close()
#include <assert.h>

int main(int argc, char *argv[]){
	int listenfd;
	struct sockaddr_in sin;
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if(listenfd == -1){
		printf("create socket error: %s (errno:%d)\n", strerror(errno), errno);
		exit(-1);
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
//	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_addr.s_addr = inet_addr("127.0.0.1");
	sin.sin_port = htons(9090);

	if(bind(listenfd, (struct sockaddr *)&sin, sizeof(sin)) == -1){
		printf("bind error: %s (errno:%d)\n", strerror(errno), errno);
		exit(-1);
	}
	char ip_check[INET_ADDRSTRLEN];
	printf("local ip: %s\n", inet_ntop(AF_INET, &sin.sin_addr, ip_check, INET_ADDRSTRLEN));
//assert(inet_ntop(AF_INET, &sin.sin_addr, ip_check, INET_ADDRSTRLEN) != NULL);

	int backlog = 10;
	if(listen(listenfd, backlog) == -1){
		printf("listen error: %s (errno:%d)\n", strerror(errno), errno);
		exit(-1);
	}

	int connfd;
	struct sockaddr_in connaddr;
	memset(&connaddr, 0, sizeof(connaddr));

	while(1){
printf("here!\n");
		socklen_t size_connaddr = sizeof(connaddr);   
		//WARNNING!!!!
		//The third argument in accept() is value-result argument. It costs me a half day to configure it out. 
		if((connfd = accept(listenfd, (struct sockaddr *)&connaddr, &size_connaddr)) < 0){
			printf("accept error: %s (errno:%d)\n", strerror(errno), errno);
			continue;
		}
		char ip_printable[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &connaddr.sin_addr, ip_printable, INET_ADDRSTRLEN);
		printf("receive connection from %s:%d\n", ip_printable, ntohs(connaddr.sin_port));
		
		if(fork() == 0){
			if(fork() == 0){
				close(listenfd);
				FILE *fp = fdopen(connfd, "r+");
				char buf[256];
//				read(connfd, buf, sizeof(buf));
//				const int LITTLESIZE = 20;
//				char redirectbuf[LITTLESIZE];
				while(1){
					if(fgets(buf, sizeof(buf), fp) == NULL){
						printf("close connection\n");
						break;
					}
//					setvbuf(stdout, redirectbuf, _IOFBF, LITTLESIZE);
					printf("%s", buf);
					if(buf[0] == '\0'){
						break;
					}
				}
				fclose(fp);
				exit(0);
			}
			close(connfd);
			exit(0);
		}
		close(connfd);
	}
	close(listenfd);
}