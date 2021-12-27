#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define PORT 8000
#define SA struct sockaddr
#define BACKLOG 512
#define NUSERS 2048
#define LOG(s) puts(s)
//#define LOG(s) ;

/* forwards */
static int conn_index(int);
static int conn_add(int);
static int conn_delete(int);
void diep(const char* s);
void read_file();
void watch_loop(int kq);

/* globals */
struct uc {
    int uc_fd;
    char *uc_addr;
} users[NUSERS];


char file[2048];
size_t file_size;

int sockfd;

/* driver */
int main() {
    read_file();
    socklen_t len;
	struct sockaddr_in servaddr, cli;
    int kq;
    struct kevent evSet;

	// socket create and verification
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
        diep("socket()");
	} else {
        LOG("Socket successfully created.");
    }
	bzero(&servaddr, sizeof(servaddr));

	// assign IP, PORT
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(PORT);

	// Binding newly created socket to given IP and verification
	if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        diep("bind()");
	} else {
		printf("Socket successfully binded.\n");
    }

	// Now server is ready to listen and verification
	if ((listen(sockfd, BACKLOG)) != 0) {
        diep("listen()");
	} else {
        LOG("Server listening.");
    }

    kq = kqueue();
    if (kq == -1) {
        diep("kqueue()");
    }

    EV_SET(&evSet, sockfd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq, &evSet, 1, NULL, 0, NULL) == -1) {
        diep("kevent()");
    }

    watch_loop(kq);
}

void watch_loop(int kq) {
    char junk[4096];
    struct kevent evSet;
    struct kevent evList[32];
    int nev, i;
    struct sockaddr_storage addr;
    socklen_t socklen = sizeof(addr);
    int fd;

    while(1) {
        nev = kevent(kq, NULL, 0, evList, 32, NULL);
        if (nev < 1) {
            diep("kevent()");
        }
        for (i=0; i < nev; i++) {
            if (evList[i].flags & EV_EOF) {
                fd = evList[i].ident;
                EV_SET(&evSet, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                if (kevent(kq, &evSet, 1, NULL, 0, NULL) == -1) {
                    diep("kevent()");
                }
                conn_delete(fd);
                close(fd);
                LOG("disconnected.");
            } else if (evList[i].ident == sockfd) {
                fd = accept(evList[i].ident, (struct sockaddr*)&addr, &socklen);
                if (fd == -1) {
                    diep("accept()");
                }

                if (conn_add(fd) == 0) {
                    EV_SET(&evSet, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
                    if (kevent(kq, &evSet, 1, NULL, 0, NULL) == -1) {
                        diep("kevent()");
                    }
                    LOG("Connection accepted.");
                } else {
                    LOG("connection refused");
                    close(fd);
                }
            } else if (evList[i].filter == EVFILT_READ) {
                recv(evList[i].ident, junk, 4096, 0);
                send(evList[i].ident, file, file_size, 0);
            } else {
                diep("watch_loop()");
            }
        }
    }
    puts("Safely closing");
    close(sockfd);
}

/* find the index of a file descriptor or a new slot if fd=0 */
int conn_index(int fd) {
    int uidx;
    for (uidx = 0; uidx < NUSERS; uidx++)
        if (users[uidx].uc_fd == fd)
            return uidx;
    return -1;
}

/* add a new connection storing the IP address */
int conn_add(int fd) {
    int uidx;
    if (fd < 1) return -1;
    if ((uidx = conn_index(0)) == -1)
        return -1;
    if (uidx == NUSERS) {
        close(fd);
        return -1;
    }
    users[uidx].uc_fd = fd; /* users file descriptor */
    users[uidx].uc_addr = 0; /* user IP address */
    return 0;
}

/* remove a connection and close it's fd */
int conn_delete(int fd) {
    int uidx;
    if (fd < 1) return -1;
    if ((uidx = conn_index(fd)) == -1)
        return -1;

    users[uidx].uc_fd = 0;
    users[uidx].uc_addr = NULL;

    return close(fd);
}

void diep(const char *s) {
    perror(s);
    exit(EXIT_FAILURE);
}

void read_file() {
    FILE* f = fopen("index.html", "r");
    file_size = fread(file, sizeof(char), sizeof(file), f);
    fclose(f);
}

