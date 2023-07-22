//
// Created by CEN-KL on 2023/6/11.
//

#ifndef FXXKMEETING_ROOM_H
#define FXXKMEETING_ROOM_H
#include "msg.h"
#include "unpthread.h"

#define SENDTHREADSIZE 5

void process_main(int, int);
void* accept_fd(void *);
void* send_func(void *);
void  fdclose(int, int);

#endif //FXXKMEETING_ROOM_H
