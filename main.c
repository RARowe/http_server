#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <sqlite3.h>
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

typedef struct {
    pthread_mutex_t mutex;
    sem_t* sem;
    int front, back, size;
    int buffer[128];
} queue_t;

void init_queue(queue_t* q) {
    q->front = 0;
    q->back = 0;
    q->size = 0;
    sem_unlink("emptysem");
    q->sem = sem_open("emptysem", O_CREAT,  0777, 0);
    if (q->sem == SEM_FAILED) {
        diep("sem_open()");
    }
}

void enqueue(queue_t* q, int sock) {
    pthread_mutex_lock(&q->mutex);

    q->buffer[q->back] = sock;
    q->back++;
    if (q->back > 127) {
        q->back = 0;
    }
    q->size++;

    pthread_mutex_unlock(&q->mutex);
    sem_post(q->sem);
}

int dequeue(queue_t* q) {
    int r = sem_wait(q->sem);
    if (r == -1) {
        diep("sem_wait()");
    }
    pthread_mutex_lock(&q->mutex);

    int val = q->buffer[q->front];

    q->front++;
    if (q->front > 127) {
        q->front = 0;
    }
    q->size--;

    pthread_mutex_unlock(&q->mutex);
    return val;
}

/* globals */
struct uc {
    int uc_fd;
    char *uc_addr;
} users[NUSERS];

char buffers[NUSERS][8192];

char file[2048];
size_t file_size;

int sockfd;


int callback(void* p, int argc, char **argv, char **azColName);

void* consumer(void* q) {
    sqlite3 *db;
    int rc = sqlite3_open("test.db", &db);

    if (rc != SQLITE_OK) {

        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);

    }
    char* sql = "SELECT * FROM Cars";
    char* err_msg = NULL;

    char junk[4096];
    char out[10000];
    char b[8192];

    while (1) {
        b[0] = '\0';
        int sock = dequeue(q);
        int rc = sqlite3_exec(db, sql, callback, b, &err_msg);
        sprintf(out, "HTTP/1.1 200 OK\nDate: Mon, 27 Jul 2009 12:28:53 GMT\nServer: Apache/2.2.14 (Win32)\nLast-Modified: Wed, 22 Jul 2009 19:15:56 GMT\nContent-Length: %lu\nContent-Type: text/plain; charset=us-ascii\nConnection: Closed\n\n%s", strlen(b), b);

        if (rc != SQLITE_OK ) {

            fprintf(stderr, "Failed to select data\n");
            fprintf(stderr, "SQL error: %s\n", err_msg);

            sqlite3_free(err_msg);
            sqlite3_close(db);
        }

        send(sock, out, strlen(out), 0);
    }
    sqlite3_close(db);
}

int callback(void* p, int argc, char **argv, char **azColName) {
    char* b = (char*)p;
    char out[256];
    out[0] = '\0';
    int i;
    for (i = 0; i < argc; i++) {
        sprintf(out, "%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
        strcat(b, out);
    }
    strcat(b, "\n");

    return 0;
}

int init_db(void) {
    sqlite3 *db;
    char* err_msg = NULL;
    char* sql = NULL;

    int rc = sqlite3_open("test.db", &db);

    if (rc != SQLITE_OK) {

        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);

        return EXIT_FAILURE;
    }

    sql = "DROP TABLE IF EXISTS Cars;" 
        "CREATE TABLE Cars(Id INT, Name TEXT, Price INT);" 
        "INSERT INTO Cars VALUES(1, 'Audi', 52642);" 
        "INSERT INTO Cars VALUES(2, 'Mercedes', 57127);" 
        "INSERT INTO Cars VALUES(3, 'Skoda', 9000);" 
        "INSERT INTO Cars VALUES(4, 'Volvo', 29000);" 
        "INSERT INTO Cars VALUES(5, 'Bentley', 350000);" 
        "INSERT INTO Cars VALUES(6, 'Citroen', 21000);" 
        "INSERT INTO Cars VALUES(7, 'Hummer', 41400);" 
        "INSERT INTO Cars VALUES(8, 'Volkswagen', 21600);";

    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);

    if (rc != SQLITE_OK ) {

        fprintf(stderr, "SQL error: %s\n", err_msg);

        sqlite3_free(err_msg);        
        sqlite3_close(db);

        return EXIT_FAILURE;
    } 


    sqlite3_close(db);
    return EXIT_SUCCESS;
}

queue_t q;
/* driver */
int main() {
    init_queue(&q);
    pthread_t t0, t1, t2;
    pthread_create(&t0, NULL, consumer, &q);
    pthread_create(&t1, NULL, consumer, &q);
    pthread_create(&t2, NULL, consumer, &q);
    init_db();
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
                enqueue(&q, evList[i].ident);
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

