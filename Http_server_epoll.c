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
#include <assert.h>

#define BUFSIZE 4096
#define MAX_PROCESS 100
#define MAX_EVENT 50
#define FOLDER "www"
#define INDEX_FILE "/index.html"
#define FILE_404 "/404.html"
#define SERVER_STRING "Server: MyHttpd/0.4"
#define HASH_NUM 37

static int epfd;
static int num_current_process;

enum status_set {READ_REQUEST_HEADER, SEND_RESPONSE_HEADER, SEND_RESPONSE};
enum responses {s_200, s_404, s_501};

static const char *RESPONSE_TABLE[] = {	"HTTP/1.0 200 OK", 
										"HTTP/1.0 404 NOT FOUND", 
										"HTTP/1.0 501 Not Implement"};

static const char *FILE_TYPE[HASH_NUM] = {	"text/plain", "audio/mpeg", "image/gif", NULL, NULL, 
											NULL, "application/pdf", "text/plain", NULL, NULL, 
											NULL,"image/x-bmp", "video/x-msvideo", "image/jpeg", "application/exe", 
											NULL, NULL, "image/png", NULL, "application/x-tar", 
											NULL,NULL, NULL, NULL, "application/rtf", 
											NULL, "audio/wav", NULL, "applicaion/x-shockwave-flash", "text/xml", 
											NULL,"application/zip", "text/html", NULL, NULL, NULL, NULL};

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
	int cgi;
	int fd;
	int file_type;
} process_t;

void finish_task_from_processes(process_t *process);
char * get_token(char *buffer, string_t *token);
void send_response(process_t *process);
void send_response_header(process_t *process);
int check_file_existence(string_t *path);
void read_request_header(process_t *process);
process_t *add_task_to_processes(int clientfd, process_t * processes);
process_t *fetch_process_by_socket(int socket, process_t *processes);
int socket_initialization(void);
int write_until_block(process_t *process);
int read_until_block(process_t *process);
void accept_request(int listenfd, process_t *processes);
char *read_line(char *buffer, string_t *line);
void prase_querystring(char *querystring);
int get_file_type(char *path);


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
	sin.sin_port = htons(19090);
	int on = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
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
		res = read(process->socket, process->buffer+process->read_pos, BUFSIZE-process->read_pos);
		if(res == -1){
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				break;
			}else{
				perror("read");
				break;
			}
		}else if(res == 0){
			process->read_pos = 0;
			return -1;
		}
		process->read_pos += res;
		process->buffer_len = process->read_pos;
	}
	return 0;
}

//循环写入数据，直到写满缓冲区，如果阻塞返回0，如果写完返回-1
int write_until_block(process_t *process){
	int res;
printf("write:%d\n", process->buffer_len);
	while(1){
//printf("%d\n",process->write_pos);
		//当write返回0时表明可写的数据已经写完
		res = write(process->socket, process->buffer+process->write_pos, process->buffer_len - process->write_pos);
		if( res == -1){
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				break;
			}else{
				perror("write");
				break;
			}
		}else if(res == 0 && process->buffer_len == process->write_pos){
//		}else if(res == 0){
			process->write_pos = 0;
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
		int clientfd = accept(listenfd, (struct sockaddr *)&clientaddr, (socklen_t *)&size_clientaddr);
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
		event.events = EPOLLOUT|EPOLLIN|EPOLLET;
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
		process = &processes[clientfd];
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
	process->buffer_len = 0;
	process->cgi = 0;
	process->fd = -1;
	return process;
}

void read_request_header(process_t *process){
	int s = read_until_block(process);
printf("read_request_header!\n");
	if(s == -1){
		finish_task_from_processes(process);
		return;
	}

	char *p;
    //HTTP通过空行来标志请求头的结束
	//when the read string contains "\r\n\r\n", it indicats we read header completely.
	if((p = strstr(process->buffer, "\r\n\r\n")) != NULL && 
		(int)(p - process->buffer) < process->buffer_len){
printf("read header complete!\n");
		string_t method;
		char *pos = get_token(process->buffer, &method);
		string_t url;
		pos = get_token(pos, &url);
		char path_buf[BUFSIZE];

		if(strncmp(method.str, "GET", method.size) == 0){
			url.str[url.size] = '\0';
			if((p = strchr(url.str, '?')) != NULL){
				//cgi=1 indicates that this connection request cgi results.
				process->cgi = 1;
				*p = '\0';
				++p;
				prase_querystring(p);
			}
		}else if(strncmp(method.str, "POST", method.size) == 0){
			process->cgi = 1;
			string_t line;
			//discard the request line which contains METHOD 
			p = read_line(process->buffer, &line);
			while(1){
				p = read_line(p, &line);
				if(line.size == 0){
					break;
				}
				line.str[line.size] = '\0';
				putenv(line.str);
			}
		}

		//sprintf constructinig the string will add the null terminator 
		sprintf(path_buf, "%s%s", FOLDER, url.str);
		//the sizeof return a value which is the length of string plus one
		string_t path = {path_buf, sizeof(FOLDER)-1+url.size};
printf("%s\n", path.str);
		if(check_file_existence(&path)){
			if(process->cgi > 0){
				int cgi_input[2];
				int cgi_output[2];
				if( pipe(cgi_input) == -1 || pipe(cgi_output) == -1){
					perror("cgi pipe");
					// task can't be proceed.
					return;
				}
				int pid;
				if( (pid = fork()) == 0){
					close(cgi_input[1]);
					dup2(cgi_input[0], 0);
					close(cgi_output[0]);
					dup2(cgi_output[1], 1);
					//cgi outputs result in kernel buffer, and can be retrieved later.
					execl(path.str, path.str, NULL);
				}
				close(cgi_input[0]);
				close(cgi_output[1]);
				if(set_socket_nonblock(cgi_output[0]) == -1){
					return;
				}

				struct epoll_event event;
				memset(&event, 0, sizeof(event));
				event.data.fd = process->socket;
				event.events = EPOLLIN|EPOLLET;
				epoll_ctl(epfd, EPOLL_CTL_ADD, cgi_output[0], &event);
				//if some arguments need to be sent to cgi program, use cgi_input[1] before close it.
				close(cgi_input[1]);
				process->cgi = cgi_output[0];
			}else{
printf("%s", path.str);
				int fd = open(path.str, O_RDONLY);
				process->fd = fd;
				process->response_code = s_200;
			}
		}else{
			sprintf(path.str, "%s%s", FOLDER, FILE_404);
			process->fd = open(path.str, O_RDONLY);
printf("%s\n", path.str);
			process->response_code = s_404;
		}
		if(process->cgi == 0){
			p = strchr(path.str, '.');
printf("file-type: %s\n", p+1);
			process->file_type = get_file_type(p+1);
		}

printf("send_response_header!\n");
		process->status = SEND_RESPONSE_HEADER;
		send_response_header(process);
	}
}

int get_file_type(char *path){
	int result = 0;
	while(*path != '\0'){
		result += *path -'a'; 
		++path;
	}
	result = (result + 20) % HASH_NUM;
	return result;
}

void prase_querystring(char *querystring){
	char *word;
	char *p = querystring;
	while(1){
		word = p;
		while(*p != '&' && *p != '\0'){
			++p;
		}
		if(*p == '\0'){
			putenv(word);
			break;
		}
		*p = '\0';
		putenv(word);
		++p;
	}
}

char *read_line(char *buffer, string_t *line){
	if(*buffer == '\0'){
		return NULL;
	}
	line->str = buffer;
	while(*buffer != '\r' && *buffer != '\n'){
		++buffer;
	}
	line->size = buffer - line->str;
	if(*buffer == '\r'){
		++buffer;
		if(*buffer == '\n'){
			++buffer;
		}
	}else{
		++buffer;
	}
	return buffer;
}


// validate file path and add index filename if it is a directory
int check_file_existence(string_t *path){
	struct stat stat_buf;
	int s;

	path->str[path->size] = '\0';
	s = lstat(path->str, &stat_buf);
	if(s == -1){
		return 0;
	}
	if(S_ISDIR(stat_buf.st_mode)){
		if(path->str[path->size-1] == '/'){
			path->size--;
		}
		strcpy(path->str+path->size, INDEX_FILE);
		path->size += sizeof(INDEX_FILE);
		s = lstat(path->str, &stat_buf);
		if(s == -1){
			return 0;
		}
	}
	return 1;
}

void send_response_header(process_t *process){
printf("file-type: %d\n", process->file_type);
	const char *content_type = process->cgi > 0?FILE_TYPE[0]:FILE_TYPE[process->file_type];
	sprintf(process->buffer, "%s\r\n%s\r\nContent-Type: %s\r\n\r\n", 
							RESPONSE_TABLE[process->response_code], SERVER_STRING,content_type);
	//Warning!! Don't use sizeof to calculate buffer_len because sizeof don't know the RESPONSE_TABLE[] point to 
	process->buffer_len = strlen(process->buffer);

	int s = write_until_block(process);
	if( s == -1){
		process->status = SEND_RESPONSE;
		send_response(process);
	}
}

void send_response(process_t *process){
	int res;
	while(1){
printf("send_response!\n");
		if(process->cgi > 0){
printf("cgi:%d\n", process->cgi);
			res = read(process->cgi, process->buffer, BUFSIZE);
		}else{
			//fetch file content to buffer 
			res = read(process->fd, process->buffer, BUFSIZE);	
		}
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
		process->fd = -1;
	}
	if(process->cgi > 0){
		close(process->cgi);
		epoll_ctl(epfd, EPOLL_CTL_DEL, process->cgi, NULL);
	}
	epoll_ctl(epfd, EPOLL_CTL_DEL, process->socket, NULL);
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
		processes[i].socket = -1;
	}
	num_current_process = 0;

	struct epoll_event events[MAX_EVENT];
	while(1){
		int n = epoll_wait(epfd, events, MAX_EVENT, -1);
		if( n == -1){
			perror("epoll_wait");
		}
		for(int i=0; i < n; ++i){
//			if((events[i].events & EPOLLERR)
//				|| (events[i].events & EPOLLHUP)){
			// EPOLLHUP can't be checked because it will be up when the peer close its fd in pipe mode.
			if(events[i].events & EPOLLERR){
				printf("epoll error condition\n");
				continue;
			}
			if(events[i].data.fd == listenfd){
				accept_request(listenfd, processes);
printf("accept request!!\n");
			}else{
				process_t *process = fetch_process_by_socket(events[i].data.fd, processes);
                //使用有限状态机负责状态的记录和状态之间的转换
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
					default:
						break;
				}
			}

		}
	}
}
