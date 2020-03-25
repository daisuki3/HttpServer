#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

#define MAX_PATH 4096
#define MAX_BUFF 8192
/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

void send_request_data(int client_fd, int req_fd);
void http_not_found(int fd);
int	if_has_index_html(char *path, struct stat *req_file_stat);
void deal_dir_path(char * path);
int tcp_connect(const char *hostname, const int serv_port);
void http_send_file(int fd, int req_fd, struct stat *file_stat, char *path);
void http_send_directory(int fd, char *path);

void handle_files_request(int fd);
void handle_proxy_request(int fd);

void* server_thread_func(void *arg);
void init_thread_pool(int num_threads, void (*request_handler)(int));

void serve_forever(int *socket_number, void (*request_handler)(int));
void signal_callback_handler(int signum);
void exit_with_usage();

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */

void send_request_data(int client_fd, int req_fd){
	char *buf = (char *)malloc(MAX_BUFF + 1);
	ssize_t nread;

	while((nread = read(req_fd, buf, MAX_BUFF)) > 0)
			http_send_data(client_fd, buf, nread);

	free(buf);
}

void http_not_found(int fd){
	http_start_response(fd, 200);
 	http_send_header(fd, "Content-Type", "text/html");
  	http_end_headers(fd);
  	http_send_string(fd,
      "<center>"
      "<h1>Welcome to httpserver!</h1>"
      "<hr>"
      "<p>Nothing's here yet.</p>"
      "</center>");

	return;
}

void http_send_file(int fd, int req_fd, struct stat *file_stat, char *path){

		char file_size[100];
		sprintf(file_size, "%ld", file_stat -> st_size);

		if(req_fd != -1){
				
				http_start_response(fd, 200);
				http_send_header(fd, "Content_type", http_get_mime_type(path));
				http_send_header(fd, "Content_length", file_size);
				http_end_headers(fd);
				send_request_data(fd, req_fd);				

		}
		else
				http_not_found(fd);

}

void http_send_directory(int fd, char *path){
	
	DIR *dir = opendir(path);
	struct dirent *dir_entry;
	struct stat file_stat;
	
	char *full_path = (char*)malloc(MAX_PATH + 1);
	char *entry_link = (char*)malloc(MAX_PATH + 1);
	
  	http_start_response(fd, 200);
  	http_send_header(fd, "Content-type", "text/html");
  	http_send_header(fd, "Server", "httpserver/1.0");
  	http_end_headers(fd);

  	http_send_string(fd, "<h2>Index of ");
  	http_send_string(fd, path);
 	http_send_string(fd, " </h2><br>\n");	

  	if (dir != NULL) {
    	while ((dir_entry = readdir(dir)) != NULL) {
      			
				strcpy(full_path, path);
      			
				if(full_path[strlen(full_path) - 1] != '/') 
        				strcat(full_path, "/");
      	
      			strcat(full_path, dir_entry->d_name);
      			stat(full_path, &file_stat);
      	
				if(S_ISDIR(file_stat.st_mode)) 
        			snprintf(entry_link, MAX_PATH, "<a href='%s/'>%s/</a><br>\n", dir_entry->d_name, dir_entry->d_name);
       			else 
        			snprintf(entry_link, MAX_PATH, "<a href='./%s'>%s/</a><br>\n", dir_entry->d_name, dir_entry->d_name);

	   			
			http_send_string(fd, entry_link);
    }
    
		closedir(dir);
  }
  free(entry_link);
  free(full_path);

}

int	if_has_index_html(char *path, struct stat *req_file_stat){
	//return -1 if dir doesn't have index.html
	
		int fd;
		char *filename = (char*)malloc(MAX_PATH + 1);

		strcpy(filename, path);
		strcat(filename, "index.html");
		
		fd = open(filename, O_RDONLY);
		
		free(filename);

		return fd;

}

void deal_dir_path(char * path){
	
	int len = strlen(path);

	if(path[len - 1] != '/')	
		strcat(path,"/");
	return;

}

void handle_files_request(int fd) {

	struct http_request *request = http_request_parse(fd);
	
	if(!request){
			http_not_found(fd);
			return;
	}
	
	struct stat req_file_stat;
	int req_fd;
	char *path = (char *)malloc(MAX_PATH+1);
	
	/*   path = /etc/yh
	 *   server_files_directory = /files
	 *   request -> path = / 
	 *					 = /index.html
	 *					 = /my_documents
	 */
	getcwd(path, sizeof(char) * (MAX_PATH + 1));
	strcat(path, server_files_directory); 
	strcat(path, request -> path);

	printf("path = %s\n",path);
	
	if(stat(path, &req_file_stat) == 0){
			if(S_ISREG(req_file_stat.st_mode)){

				req_fd = open(path, O_RDONLY);
				http_send_file(fd, req_fd, &req_file_stat, path);
			
			}
			else if(S_ISDIR(req_file_stat.st_mode)){
			//make sure dir end with  "/"	
				deal_dir_path(path);

				req_fd = if_has_index_html(path, &req_file_stat);
				
				if(req_fd != -1){
			
					strcat(path, "index.html");
					if(stat(path, &req_file_stat) == 0)
						http_send_file(fd, req_fd, &req_file_stat, path);
					else
						http_not_found(fd);

				}
				else
					http_send_directory(fd, path);
		
			}
			else
				http_not_found(fd);
		
		}
	else
		http_not_found(fd);

	free(path);
}


/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */

int tcp_connect(const char *hostname, const int serv_port){
	int sock_fd, error_n;
	char serv[32];
	struct addrinfo hints, *res, *res_save;

	bzero(&hints, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	sprintf(serv, "%d", serv_port);
	printf("connect %s %s\n", hostname, serv);

	if((error_n = getaddrinfo(hostname, serv, &hints, &res)) != 0){
		fprintf(stderr, "tcp connect error for %s %s : %s", hostname, serv, gai_strerror(error_n));
		exit(1);
	}

	res_save = res;

	do{
		sock_fd = socket(res -> ai_family, res -> ai_socktype, res -> ai_protocol);

		if(sock_fd < 0) continue;
		if(connect(sock_fd, res -> ai_addr, res -> ai_addrlen) == 0) break;

		if(close(sock_fd) != 0){
			fprintf(stderr, "socket close error for %s %s\n", hostname, serv);
			exit(1);
		}
	}
	while((res = res -> ai_next) != NULL);

	if(res == NULL){
		fprintf(stderr,"tcp connect error for %s %s\n", hostname, serv);
		exit(1);
	}
	
	freeaddrinfo(res_save);

	return sock_fd;
}

void handle_proxy_request(int fd) {

  int proxy_fd = tcp_connect(server_proxy_hostname, server_proxy_port);
  int connection_status = proxy_fd;

  int client_end, maxfdp1;
  fd_set rset;
  char *client_buffer = (char *)malloc(MAX_BUFF + 1);
  char *proxy_buffer = (char *)malloc(MAX_BUFF + 1);

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(fd);

    http_start_response(fd, 502);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
    return;
  }
  
  client_end = 0;
  FD_ZERO(&rset);
  maxfdp1 = (fd > proxy_fd ? fd + 1 : proxy_fd + 1); 
  FD_SET(proxy_fd, &rset);
  FD_SET(fd, &rset);
  
  
  while(1){
			
		  if(select(maxfdp1, &rset, NULL, NULL, NULL) < 0){
		  		perror("select error\n");
				exit(1);
		  }
		  
		  int nread;

		  if(FD_ISSET(fd, &rset)){
				  if((nread == read(fd, client_buffer, MAX_BUFF)) == 0){
						  
						  client_end = 1;
						  
						  if(shutdown(fd, SHUT_WR) < 0){
								  perror("shutdown client error");
								  exit(1);
						  }
						  
						  FD_CLR(fd, &rset);
						  continue;
		  		  }
				  else if(nread < 0){
				  	perror("read from client error");
					exit(1);
				  }
				  http_send_data(proxy_fd, client_buffer, nread);
  		}

		if(FD_ISSET(proxy_fd, &rset)){
				if((nread = read(proxy_fd, proxy_buffer, MAX_BUFF)) == 0){
					if(client_end == 1){
						free(client_buffer);
						free(proxy_buffer);
						
						if(close(proxy_fd) < 0)
							perror("proxy connet close error");
						
						exit(1);
					}
					else{
						perror("proxy server has closer");
						exit(1);
					}
				}
				else if(nread < 0){
					perror("read from proxy error");
					exit(1);
				}

				http_send_data(fd, proxy_buffer, nread);
		}
	}
}

void proxy_client_func(void *arg){

}

void* server_thread_func(void *arg){
	
	void (*request_handler)(int) = arg;
	
	int client_socket_fd;
	printf("server thread func serving!!!\n");

	while(1){
		
		int client_socket_fd = wq_pop(&work_queue);
		
		request_handler(client_socket_fd);

	}
}

void init_thread_pool(int num_threads, void (*request_handler)(int)) {
	
	
	pthread_t *thread_ids = (pthread_t *)malloc(sizeof(pthread_t) * num_threads);
	
	int i;
	for(i = 0; i < num_threads; ++i)
		pthread_create(&thread_ids[num_threads - 1], NULL, server_thread_func, request_handler);
		
}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {
	
	struct sockaddr_in server_address,client_address;
	size_t client_address_length = sizeof(client_address);
	int client_socket_number;

	*socket_number = socket(AF_INET,SOCK_STREAM,0);
	if(*socket_number == -1){
			perror("Failed to creat a new socket");
			exit(errno);
	}
 
	/* Setting some mysterious settings for our socket. See man setsockopt for
     more info if curious. */
  	int socket_option = 1;
  	if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        	sizeof(socket_option)) == -1) {
    		perror("Failed to set socket options");
    		exit(errno);
  	}

	memset(&server_address,0,sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = INADDR_ANY;
	server_address.sin_port = htons(server_port);
	
	if(bind(*socket_number, (struct sockaddr *) &server_address, sizeof(server_address)) == -1){
			perror("Failed to bind on socket");
			exit(errno);
	}

	if(listen(*socket_number,1024) == -1){
			perror("Failed to listen on socket");
			exit(errno);
	}

	printf("Listening on port %d...\n", server_port);

	/* thread_creat() 
	 *
	 * 
	 */
	
	wq_init(&work_queue);

  	init_thread_pool(num_threads, request_handler);

  	while (1) {
		client_socket_number = accept(*socket_number, (struct sockaddr *)&client_address, (socklen_t *)&client_address_length);
		if(client_socket_number < 0){
			perror("Error accepting socket");
			continue;
		}
		
		wq_push(&work_queue, client_socket_number);

		printf("Accepted connection from %s on port %d\n", inet_ntoa(client_address.sin_addr),client_address.sin_port);

		
		
		
		/*
		pid = fork();

		if(pid > 0)
			close(client_socket_number);
		else if(pid == 0){
			signal(SIGINT,SIG_DFL);
			close(*socket_number);
			request_handler(client_socket_number);
			close(client_socket_number);
			exit(EXIT_SUCCESS);
		}
		else{
			perror("Failed to fork child");
			exit(errno);
	}
	*/
  }
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing  socket %d\n",server_fd);
  if(close(server_fd) < 0) 
		  perror("Failed to close server_fd(ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  /* Registering signal handler. When user enteres Ctrl-C, signal_callback_handler will be invoked. */
  struct sigaction sa;
  sa.sa_flags = SA_RESTART;
  sa.sa_handler = &signal_callback_handler;
  sigemptyset(&sa.sa_mask);
  sigaction (SIGINT, &sa, NULL);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
