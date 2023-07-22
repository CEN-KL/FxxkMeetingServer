//
// Created by CEN-KL on 2023/6/11.
//

#ifndef FXXKMEETING_ROOM_H
#define FXXKMEETING_ROOM_H
#include "msg.h"
#include "unp.h"
#include <pthread.h>
#include <mutex>
#include <memory>

#define SENDTHREADSIZE 5

struct Process{
    pid_t child_pid;   // 子进程id
    int child_pipefd;  // 父子进程之间通信的描述符
    int child_status;  // 0 = ready
    int total;
};

struct Room {  // single
    int navail;  // 可用房间数
    Process *pptr;
    std::mutex m_mtx;

    Room(int n) :navail(n) {
        pptr = (Process *) Calloc(n, sizeof(Process));
    }
};

void process_main(int, int);
void accept_fd(int);
void send_func();
void fdclose(int, int);
void thread_main();



#endif //FXXKMEETING_ROOM_H
