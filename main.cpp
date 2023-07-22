#include <iostream>
#include "unpthread.h"
#include "room.h"

Thread *tptr;
socklen_t addrlen = sizeof(sockaddr);
int listenfd;
int nprocesses, nthreads;
Room *room;
void sig_chld(int signo);
void thread_make(int);
void process_make(int, int);

int main(int argc, char **argv) {
    Signal(SIGCHLD, sig_chld);

    fd_set rset, masterset;
    FD_ZERO(&masterset);

    // 打开监听套接字
    if (argc == 4) {
        listenfd = Tcp_listen(NULL, argv[1], &addrlen);
    } else if (argc == 5) {
        listenfd = Tcp_listen(argv[1], argv[2], &addrlen);
    } else {
        err_quit("usage: ./app [host] <port #> <#threads> <#processes>");
    }

    int maxfd = listenfd;
    nthreads = atoi(argv[argc - 2]);
    nprocesses = atoi(argv[argc - 1]);

    // 管理房间（一个房间即一个进程)
    room = new Room(nprocesses);
    printf("total threads: %d   total processes: %d\n", nthreads, nprocesses);

    tptr = (Thread *) Calloc(nthreads, sizeof(Thread));

    // 房间进程池
    for (int i = 0; i < nprocesses; i++) {
        process_make(i, listenfd);
        FD_SET(room->pptr[i].child_pipefd, &masterset);
        maxfd = std::max(maxfd, room->pptr[i].child_pipefd);
    }

    // 线程池
    for (int i = 0; i < nthreads; i++) {
        thread_make(i);
    }

    while (true) {
        rset = masterset;   // 深拷贝

        int nsel = Select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nsel == 0) continue;

        // 处理房间进程向他们的父进程（也就是main函数进程）传递的消息：1、房间已空（E）; 2、有人退出（Q）
        for (int i = 0; i < nprocesses; i++) {
            if (FD_ISSET(room->pptr[i].child_pipefd, &rset)) {
                char rc;
                if (Readn(room->pptr[i].child_pipefd, &rc, 1) <= 0) {
                    err_quit("child %d terminated unexpectedly", i);
                }
                printf("c = %c\n", rc);
                if (rc == 'E') {
                    // 房间变空
                    Pthread_mutex_lock(&room->lock);
                    room->pptr[i].child_status = 0;
                    ++room->navail;
                    printf("room %d is now free\n", room->pptr[i].child_pid);
                    Pthread_mutex_unlock(&room->lock);
                } else if (rc == 'Q') {
                    // 房间有人离开
                    Pthread_mutex_lock(&room->lock);
                    --room->pptr[i].total;
                    Pthread_mutex_unlock(&room->lock);
                } else {
                    err_msg("read from %d error", room->pptr[i].child_pipefd);
                    continue;
                }
                if (--nsel == 0) break;
            }
        }
    }
}

void process_make(int i, int listen_fd) {
    int sockfd[2];
    pid_t pid;

    Socketpair(AF_LOCAL, SOCK_STREAM, 0, sockfd);  // 创建一对全双工套接字sockfd[2]
    if ((pid = fork()) > 0) {
        Close(sockfd[1]);
        room->pptr[i].child_pid = pid;
        room->pptr[i].child_pipefd = sockfd[0];  // 父进程（也即main函数进程）会在无限循环中监听子进程的sockfd[0]
        room->pptr[i].child_status = 0;
        room->pptr[i].total = 0;
        return; // 父进程返回main函数
    }

    Close(listen_fd);
    Close(sockfd[0]);
    process_main(i, sockfd[1]);    // sockfd[1]是子进程与父进程进行通信的套接字
}

void thread_make(int i) {
    int *arg = (int *) Calloc(1, sizeof(int));
    *arg = i;
    Pthread_create(&tptr[i].thread_tid, NULL, thread_main, arg);
}


