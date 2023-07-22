//
// Created by CEN-KL on 2023/6/11.
//

#ifndef FXXKMEETING_UNPTHREAD_H
#define FXXKMEETING_UNPTHREAD_H

#include <pthread.h>
#include "unp.h"

typedef void *(THREAD_FUNC)(void *);

typedef struct {
    pthread_t thread_tid;
} Thread;

typedef struct {
    pid_t child_pid;   // 子进程id
    int child_pipefd;  // 父子进程之间通信的描述符
    int child_status;  // 0 = ready
    int total;
} Process;

struct Room {  // single
    int navail;  // 可用房间数
    Process *pptr;
    pthread_mutex_t lock;

    Room(int n) :navail(n) {
//        navail = n;
        pptr = (Process *) Calloc(n, sizeof(Process));
        lock = PTHREAD_MUTEX_INITIALIZER;
    }
};

void Pthread_create(pthread_t *tid, const pthread_attr_t *attr, THREAD_FUNC *func, void *arg);
void Pthread_detach(pthread_t tid);
void Pthread_mutex_lock(pthread_mutex_t *mptr);
void Pthread_mutex_unlock(pthread_mutex_t *mptr);
void Pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *lock);
void Pthread_cond_signal(pthread_cond_t *cond);
void *thread_main(void *);

#endif //FXXKMEETING_UNPTHREAD_H
