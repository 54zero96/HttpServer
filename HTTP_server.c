#include <stdio.h>
#include <stdlib.h> //perror
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h> //strerror, memset

#define SERVER_STRING "MyHttp/0.5\r\n"
#define FOLDER "www"

enum status_code {s_200, s_404, s_501};
const char *response_lines[] = {"HTTP/1.0 200 OK", "HTTP/1.0 404 NOT FOUND", "HTTP/1.0 501 Method Not Implemented"};    //常量字符串需定义为const char *

struct connection_s {
	
	enum status_code status;
};

int socket_initialization(void);
void accept_request(int clientfd);
int get_line(int fd, char *line, int line_len);
void not_found(int clientfd);
void fetch_file(int clientfd, char *path);
void header(int clientfd, const char *first_line);
void fetch_resource(int clientfd, FILE *resource);
char *getToken(char *str, char *token, int t_len);

int socket_initialization(void){
    int listenfd;
	struct sockaddr_in sin;
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if(listenfd == -1){
		perror("open socket error");
		return -1;
	}

	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(9090);
	if(bind(listenfd, (struct sockaddr *)&sin, sizeof(sin)) == -1){
		perror("bind error");
		return -1;
	}

	int backlog = 10;
	if(listen(listenfd, backlog) == -1){
		perror("listen error");
		return -1;
	}
	return listenfd;
}

void accept_request(int clientfd){
	char path[255];
	char line[1024];
	char method[255];
	char url[255];
	int numread = 0;
	numread = get_line(clientfd, line, sizeof(line));
	char *pos = getToken(line, method, sizeof(method));
	pos = getToken(pos, url, sizeof(url));

	//Method not implemented.
	if(strcasecmp(method, "POST") != 0 && strcasecmp(method, "GET") != 0){
		strcpy(url, "/501.html");
	}
	if(strcasecmp(method, "GET") == 0){

	}
	if(strcmp(url,"/") == 0){
		strcpy(url+1, "index.html");
	}
	sprintf(path, "%s%s", FOLDER, url);
	fetch_file(clientfd, path);
}

//fetch static file with GET
void fetch_file(int clientfd, char *path, enum status){
	enum status_code status;
	char buf[1024];
	buf[0] = 'A';
	buf[1] = '\0';
	int numread = 1;
	while(numread > 0 && strcmp(buf,"\n")){
		get_line(clientfd, buf, sizeof(buf));
	}

	FILE *resource = fopen(path, "r");
	if(resource == NULL){
		sprintf(path, "%s%s", FOLDER, "/404.html");
		resource = fopen(path, "r");
		status = s_404;
	}else{
		status = s_200;
	}
	header(clientfd, response_lines[status]);
	fetch_resource(clientfd, resource);
	fclose(resource);
}

//获取文件的返回的应答的Header
void header(int clientfd, const char *first_line){
	char buf[1024];
	sprintf(buf, "%s\r\n", first_line);
	send(clientfd, buf, strlen(buf), 0);
	strcpy(buf, SERVER_STRING);
	send(clientfd, buf, strlen(buf), 0);
	strcpy(buf, "Content-Type: text/html\r\n");
	send(clientfd, buf, strlen(buf), 0);
	strcpy(buf, "\r\n");
	send(clientfd, buf, strlen(buf), 0);
}

// 获取文件指针所指向的文件资源
void fetch_resource(int clientfd, FILE *resource){
	char buf[1024];
	while(!feof(resource)){
		fgets(buf, sizeof(buf), resource);
		send(clientfd, buf, strlen(buf), 0);
	}
}

// return value is the ptr to the current position to str
char *getToken(char *str, char *token, int t_len){
	int i = 0;
	int j = 0;
	while(str[i] == ' '){
		i++;
	}
	while(str[i] != ' ' && str[i] != '\0'){
		token[j++] = str[i++];
		if(j >= t_len-1) break;
	}
	token[j] = '\0';
	return str+i+1;
}

int get_line(int fd, char *line, int line_len){
	char c = '\0';
	int n = 0;
	int i = 0;
	while(c != '\n' && i < line_len-1){
		n=recv(fd, &c, 1, 0);
		if(n == 0){
			c = '\n';
		}else if(c == '\r'){
			n = recv(fd, &c, 1, MSG_PEEK);
			if(n == 1 && c == '\n'){
				recv(fd, &c, 1, 0);
			}
			c = '\n';
		}
		line[i++] = c;
	}
	line[i] = '\0';
	return i;
}

int main(int argc, char *argv[]){
	int listenfd = socket_initialization();
	if(listenfd == -1){
		return -1;
	}

	while(1){
		int connfd;
		struct sockaddr_in clientaddr;
		int clientaddr_size = sizeof(clientaddr);
		memset(&clientaddr, 0, sizeof(clientaddr));

		connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientaddr_size);
		if(connfd == -1){
			continue;
		}
		char ip_printable[INET_ADDRSTRLEN];
		printf("Receive connection from %s:%d\n", inet_ntop(AF_INET, &clientaddr.sin_addr, ip_printable, INET_ADDRSTRLEN), 
													ntohs(clientaddr.sin_port));
		if(fork() == 0){
			close(listenfd);
			struct connection_s conn;
			accept_request(connfd);
			return 0;
		}
		close(connfd);
	}
	close(listenfd);

	return 0;
}