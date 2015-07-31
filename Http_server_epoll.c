#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h> // F_GETFL
#include <netinet/tcp.h> // TCP_CORK
#include <sys/epoll.h>

#define BUFSIZE 1024
#define MAX_PROCESS 100
#define MAX_EVENT 50
#define FOLDER "www"
#define INDEX_FILE "/index.html"
#define FILE_404 "/404.html"
#define SERVER_STRING "Server: MyHttpd/0.4"

namespace tes{
	static int epfd;
	static int num_current_process;
}
using namespace tes;

enum status_set {READ_REQUEST_HEADER, SEND_RESPONSE_HEADER, SEND_RESPONSE};
enum responses {s_200, s_404, s_501};

static const char *RESPONSE_TABLE[] = {"HTTP/1.0 200 OK", "HTTP/1.0 404 NOT FOUND", "HTTP/1.0 501 Not Implement"};

typedef struct{
	char *str;
	int size;
} string_t;

typedef struct{
	int socket;
	enum status_set status;
	enum responses response_code;
	char buffer[BUFSIZE];
	int buffer_len;
	int read_pos;
	int write_pos;
	int fd;
} process_t;

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

//循环读入数据，直到读空缓冲区，如果阻塞返回0，如果对端关闭连接返回-1
int read_until_block(process_t *process){
	int res;
	while(1){
		res = read(process->fd, process->buffer+process->read_pos, BUFSIZE-process->read_pos);
		if(res == -1){
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				break;
			}else{
				perror("read");
				break;
			}
		}else if(res == 0){
			return -1;
		}
		process->read_pos += res;
	}
	return 0;
}

//循环写入数据，直到写满缓冲区，如果阻塞返回0，如果写完返回-1
int write_until_block(process_t *process){
	int res;
	while(1){
		//当write返回0时表明可写的数据已经写完
		res = write(process->socket, process->buffer+process->write_pos, process->buffer_len - process->write_pos);
		if( res == -1){
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				break;
			}else{
				perror("write");
				break;
			}
		}else if(res == 0){
			return -1;
		}
		process->write_pos += res;
	}
	return 0;
}

int set_socket_nonblock(int socket){
	int flag;
	flag = fcntl(socket, F_GETFL, 0);
	if(flag == -1){
		flag = 0;
	}
	flag |= O_NONBLOCK;
	int result = fcntl(socket, F_SETFL, flag);
	return result;
}

void accept_request(int listenfd, process_t *processes){
	struct sockaddr_in clientaddr;
	int size_clientaddr = sizeof(clientaddr);
	while(1){
		int clientfd = accept(listenfd, (struct sockaddr *)&clientaddr, &size_clientaddr);
		if(clientfd == -1){
			if( errno == EAGAIN || errno == EWOULDBLOCK){
				break;
			}else{
				perror("accept error");
				break;
			}
		}
		int s = set_socket_nonblock(clientfd);
		if(s == -1 || num_current_process >= MAX_PROCESS){
			close(clientfd);
			continue;
		}

//		int on = 1;
//		setsockopt(clientfd, SOL_TCP, TCP_CORK, &on, sizeof(on));
		struct epoll_event event;
		memset(&event, 0, sizeof(event));
		event.data.fd = clientfd;
		event.events = EPOLLIN|EPOLLET;
		epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &event);
		process_t *process = add_task_to_processes(clientfd, processes);
		read_request_header(process);
	}
}

// 为新连接寻找一个新的存储空间
process_t *add_task_to_processes(int clientfd, process_t * processes){
	process_t *process;
	// process结构体中socket为-1表示该空间未被占用
	if(clientfd < MAX_PROCESS && processes[clientfd].socket == -1){
		process = processes[clientfd];
	}else{
		for(int i = 0; i < MAX_PROCESS; ++i){
			if( processes[i].socket == -1){
				process = &processes[i];
				break;
			}
		}
	}
	num_current_process++;
	process->socket = clientfd;
	process->read_pos = 0;
	process->write_pos = 0;
	process->status = READ_REQUEST_HEADER;
	process->fd = -1;
	return process;
}

void read_request_header(process_t *process){
	int s = read_until_block(process);
	if(s == -1){
		finish_task_from_processes(process);
		return;
	}else{
		if(strstr(process->buffer, "\r\n\r\n") != NULL){
			string_t method;
			char *pos = get_token(process->buffer, &token);

			if(strncmp(token.str, "GET", token.size) == 0){

			}
			string_t url;
			char path_buf[BUFSIZE];
			pos = get_token(pos, &url);
			url->str[url->size] = '\0';
			sprintf(path_buf, "%s%s", FOLDER, url->str);
			string_t path = {path_buf, sizeof(FOLDER)+url->size};
			if(is_file_existence(&path) == 0){
				int fd = open(path->str, "r");
				process->fd = fd;
				process->response = s_200;
			}else{
				sprintf(path_buf, FOLDER, FILE_404);
				process->fd = open(path_buf, "r");
				process->response = s_404;
			}
			send_response_header(process);
		}
	}
}

int is_file_existence(string_t *path){
	struct stat stat_buf;
	int s;

	path->str[path->size] = '\0';
	s = lstat(path->str, &stat_buf);
	if(s == -1){
		return -1;
	}
	if(S_ISDIR(stat_buf.st_mode)){
		if(path_buf[path->size-1] == '/'){
			path->size--;
		}
		strcpy(path->str+path->size, INDEX_FILE);
		path->size += sizeof(INDEX_FILE);
		s = lstat(path->str, &stat_buf);
		if(s == -1){
			return -1;
		}
	}
	return 0;
}

void send_response_header(process_t *process){
	sprintf(process->buffer, "%s\r\n%s\r\n%s\r\n", RESPONSE_TABLE[process->response_code], SERVER_STRING,
													"Content-Type: text/html");
	process->buffer_len = sizeof(RESPONSE_TABLE[process->response_code]) +
							sizeof(SERVER_STRING) + sizeof("Content-Type: text/html") + 6;
	int s = write_until_block(process);
	if( s == -1){
		process->status = SEND_RESPONSE;
		send_response(process);
	}
}

void send_response(process_t *process){
	while(1){
		int res = read(process->fd, process->buffer, BUFSIZE);
		if(res == 0){
			finish_task_from_processes(process);
			return;
		}
		if(res == -1){
			break;
		}
		process->buffer_len = res;
		res = write_until_block(process);
		if(res == 0){
			break;
		}else{
			process->write_pos = 0;
		}
	}
}

char * get_token(char *buffer, string_t *token){
	char *c = buffer;
	while(*c == ' '){
		++c;
	}
	token->str = c;
	while(*c != ' '){
		++c;
	}
	token->size = c - token->str;
	return c;
}

void finish_task_from_processes(process_t *process){
	if(process->fd != -1){
		close(process->fd);
		process->fd = -1
	}
	num_current_process--;
	close(process->socket);
	process->socket = -1;
}

process_t *fetch_process_by_socket(int socket, process_t *processes){
	if(socket < MAX_PROCESS || processes[socket].socket == socket){
		return &processes[socket];
	}
	for(int i = 0; i < MAX_PROCESS; ++i){
		if(processes[i].socket == socket){
			return &processes[i];
		}
	}
}

int main(void){
	int listenfd = socket_initialization();
	if(listenfd == -1){
		exit(-1);
	}
	if(set_socket_nonblock(listenfd) == -1){
		exit(-1);
	}
	epfd = epoll_create(1);

	struct epoll_event event;
	memset(&event, 0, sizeof(event));
	event.data.fd = listenfd;
	event.events = EPOLLIN|EPOLLET;
	epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &event);

	process_t processes[MAX_PROCESS];
	for(int i = 0; i < MAX_PROCESS; ++i){
		process[i]->socket = -1;
	}
	num_current_process = 0;

	struct epoll_event events[MAX_EVENT];
	while(1){
		int n = epoll_wait(epfd, events, MAX_EVENT, -1);
		if( n == -1){
			perror("epoll_wait");
		}
		for(int i=0; i < n; ++i){
			if((events[i].events & EPOLLERR)
				|| (events[i].events & EPOLLHUP)){
				printf("epoll error condition\n");
				continue;
			}
			if(events[i].data.fd == listenfd){
				accept_request(listenfd, processes);
			}else{
				process_t *process = fetch_process_by_socket(events[i].data.fd, processes);
				switch(process->status){
					case READ_REQUEST_HEADER:
						read_request_header(process);
						break;
					case SEND_RESPONSE_HEADER:
						send_response_header(process);
						break;
					case SEND_RESPONSE:
						send_response(process);
						break;
					default;
						break;
				}
			}

		}
	}
}