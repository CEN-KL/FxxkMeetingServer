//
// Created by CEN-KL on 2023/6/11.
//
#include "room.h"
#include "unp.h"
#include <map>
#include <iostream>

SEND_QUEUE send_queue; // save data

enum USER_TYPE {
    GUEST = 2,
    OWNER,
};

static volatile int maxfd;
STATUS volatile roomStatus = ON;

uint32_t getpeerip(int);

typedef struct pool {
    fd_set fdset;
    pthread_mutex_t lock;
    int owner;
    int num;
    int status[1024 + 10];
    std::map<int, uint32_t> fdToIp;

    pool() {
        memset(status, 0, sizeof(status));
        owner = 0;
        FD_ZERO(&fdset);
        lock = PTHREAD_MUTEX_INITIALIZER;
        num = 0;
    }

    void clear_room() {
        Pthread_mutex_lock(&lock);
        roomStatus = CLOSE;
        for (int i = 0; i <= maxfd; i++) {
            if (status[i] == ON)
                Close(i);
        }
        memset(status, 0, sizeof(status));
        num = 0;
        owner = 0;
        FD_ZERO(&fdset);
        fdToIp.clear();
        send_queue.clear();
        Pthread_mutex_unlock(&lock);
    }
} Pool;

Pool *user_pool = new Pool();

// in process_make:  process_main(i, sockfd[1]);
void process_main(int _i, int fd) {
    printf("room %d starting \n", getpid());
    Signal(SIGPIPE, SIG_IGN);

    pthread_t pfd1;
    int *ptr = (int *) malloc(4);
    *ptr = fd;
    // 负责监听`doWithUser`里传递的消息（包含新到的客户端连接的描述符）
    Pthread_create(&pfd1, NULL, accept_fd, ptr);
    // 多线程负责发送消息
    for (int i = 0; i < SENDTHREADSIZE; i++) {
        Pthread_create(&pfd1, NULL, send_func, NULL);
    }

    // 处理已经建立的连接里发送的消息(Select),包括文字、语音、视频帧、关闭摄像头
    while (true) {
        fd_set rset = user_pool->fdset;
        int nsel;
        struct timeval time;
        memset(&time, 0, sizeof(struct timeval));
        while ((nsel = Select(maxfd + 1, &rset, NULL, NULL, &time)) == 0)
        {
            rset = user_pool->fdset;
        }
        for (int i = 0; i <= maxfd; i++)
        {
            if (FD_ISSET(i, &rset))
            {
                char head[15] = {0};
                auto ret = Readn(i, head, 11);
                if (ret <= 0)
                {
                    printf("peer close\n");
                    fdclose(i, fd);
                }
                else if (ret == 11)
                {
                    if (head[0] == '$')
                    {
                        MSG_TYPE msg_type;
                        memcpy(&msg_type, head + 1, 2);
                        msg_type = (MSG_TYPE)ntohs(msg_type);

                        MSG msg;
                        memset(&msg, 0, sizeof(msg));
                        msg.targetfd = i;
                        memcpy(&msg.ip, head + 3, 4);
                        int msglen;
                        memcpy(&msglen, head + 7, 4);
                        msg.len = ntohl(msglen);
                        std::cout << "msg type: " << msg_type << std::endl;

                        if (msg_type == IMG_SEND || msg_type == TEXT_SEND || msg_type == AUDIO_SEND)
                        {
                            msg.msgType = (msg_type == IMG_SEND) ? IMG_RECV : ((msg_type == AUDIO_SEND) ? AUDIO_RECV : TEXT_RECV);
                            msg.ptr = (char *)malloc(msg.len);
                            msg.ip = user_pool->fdToIp[i];
                            if (Readn(i, msg.ptr, msg.len) < msg.len)
                            {
                                err_msg("3 msg format error");
                            }
                            else
                            {
                                char tail;
                                Readn(i, &tail, 1);
                                if (tail != '#')
                                {
                                    err_msg("4 msg format error");
                                }
                                else
                                {
                                    send_queue.push_msg(msg);
                                }
                            }
                        }
                        else if (msg_type == CLOSE_CAMERA)
                        {
                            char tail;
                            Readn(i, &tail, 1);
                            if (tail == '#' && msg.len == 0)
                            {
                                // std::cout << "someone close camera" << std::endl;
                                msg.msgType = CLOSE_CAMERA;
                                msg.ip = user_pool->fdToIp[i];
                                send_queue.push_msg(msg);
                            }
                            else
                            {
                                err_msg("camera data error");
                            }
                        }
                    }
                } else
                {
                    err_msg("2 msg format error");
                }
                if (--nsel <= 0) break;
            }
        }
    }
}


/*
 * 监听`dowithuser`里传递的消息（包含客户端连接的文件描述符），因此收到的消息内容为`C`或`J`，把传递过来的描述符加入到`userpool->fdset`中
 * 处理新用户创建/加入会议的事件
 */
void *accept_fd(void *arg) {
    // accept fd from farther
    Pthread_detach(pthread_self());
    int fd = *(int *) arg, tfd = -1;  // fd: sockfd[1]      tfd: thread_main传递过来的的connfd
    free(arg);
    while (1) {
        int c, n;
        // read_fd里的recvmsg会阻塞
        if ((n = read_fd(fd, &c, 1, &tfd)) <= 0)
        {
            // std::cout << "n is : " << n << std::endl;
            err_quit("read_fd error");
        }
        if (tfd < 0) {
            printf("c = %c\n", c);
            err_quit("no descriptor from read_fd");
        }

        if (c == 'C') {
            // 创建会议
            Pthread_mutex_lock(&user_pool->lock);
            FD_SET(tfd, &user_pool->fdset);
            user_pool->owner = tfd;
            user_pool->fdToIp[tfd] = getpeerip(tfd);
            user_pool->num++;
            user_pool->status[tfd] = ON;
            maxfd = MAX(maxfd, tfd);
            roomStatus = ON;
            Pthread_mutex_unlock(&user_pool->lock);

            MSG msg;
            msg.msgType = CREATE_MEETING_RESPONSE;
            msg.targetfd = tfd;
            int roomNo = htonl(getpid());
            msg.ptr = (char *) malloc(sizeof(int));
            memcpy(msg.ptr, &roomNo, sizeof(roomNo));
            msg.len = sizeof(int);
            send_queue.push_msg(msg);
        } else if (c == 'J') {
            Pthread_mutex_lock(&user_pool->lock);
            if (roomStatus == CLOSE) {
                close(tfd);
                Pthread_mutex_unlock(&user_pool->lock);
                continue;
            } else {
                FD_SET(tfd, &user_pool->fdset);
                user_pool->num++;
                user_pool->status[tfd] = ON;
                maxfd = MAX(tfd, maxfd);
                user_pool->fdToIp[tfd] = getpeerip(tfd);
                Pthread_mutex_unlock(&user_pool->lock);

                // broadcast to others
                MSG msg;
                memset(&msg, 0, sizeof(MSG));
                msg.msgType = PARTNER_JOIN;
                msg.ptr = NULL;
                msg.len = 0;
                msg.targetfd = tfd;
                msg.ip = user_pool->fdToIp[tfd];
                send_queue.push_msg(msg);

                // broadcast to others
                MSG msg1;
                memset(&msg1, 0, sizeof(MSG));
                msg1.msgType = PARTNER_JOIN2;
                msg1.targetfd = tfd;
                int sz = static_cast<int> (user_pool->num * sizeof(uint32_t));
                msg1.ptr = (char *) malloc(sizeof(sz));
                int pos = 0;
                for (int i = 0; i <= maxfd; i++) {
                    if (user_pool->status[i] == ON && i != tfd) {
                        uint32_t ip = user_pool->fdToIp[i];
                        memcpy(msg1.ptr + pos, &ip, sizeof(uint32_t));
                        pos += sizeof(uint32_t);
                        msg1.len += sizeof(uint32_t);
                    }
                }
                send_queue.push_msg(msg1);
                printf("join meeting: %d\n", msg.ip);
            }
        }
    }
    return NULL;
}

void *send_func(void *arg) {
    Pthread_detach(pthread_self());
    char *sendbuf = (char *)malloc(4 * MB);
    while (true) {
        memset(sendbuf, 0, 4 * MB);
        MSG msg = send_queue.pop_msg();
        int len = 0;

        sendbuf[len++] = '$';
        short type = htons((short)msg.msgType);
        memcpy(sendbuf + len, &type, sizeof(short));
        len += sizeof(short);

        if (msg.msgType == CREATE_MEETING_RESPONSE || msg.msgType == PARTNER_JOIN2) {
            len += 4; // skip ip
        }
        else if (msg.msgType == TEXT_RECV || msg.msgType == IMG_RECV || msg.msgType == AUDIO_RECV ||
                    msg.msgType == PARTNER_JOIN || msg.msgType == PARTNER_EXIT || msg.msgType == CLOSE_CAMERA)
        {
            memcpy(sendbuf + len, &msg.ip, sizeof(uint32_t));
            len += sizeof(uint32_t);
        }
        int msglen = htonl(msg.len);
        memcpy(sendbuf + len, &msglen, sizeof(msglen));
        len += sizeof(msglen);
        memcpy(sendbuf + len, msg.ptr, msg.len);
        len += msg.len;
        sendbuf[len++] = '#';

        Pthread_mutex_lock(&user_pool->lock);
        if (msg.msgType == CREATE_MEETING_RESPONSE)
        {
            if (writen(msg.targetfd, sendbuf, len) < 0)
            {
                err_msg("writen error");
            }
        }
        else if (msg.msgType == PARTNER_JOIN || msg.msgType == TEXT_RECV || msg.msgType == IMG_RECV || msg.msgType == AUDIO_RECV || msg.msgType == PARTNER_EXIT || msg.msgType == CLOSE_CAMERA)
        {
            // 通知同房间的所有人
            for (int i = 0; i <= maxfd; i++)
            {
                if (user_pool->status[i] == ON && msg.targetfd != i)
                {
                    if(writen(i, sendbuf, len) < 0)
                    {
                        err_msg("writen error");
                    }
                }
            }
        }
        else if (msg.msgType == PARTNER_JOIN2)
        {
            if (user_pool->status[msg.targetfd] == ON)
            {
                if (writen(msg.targetfd, sendbuf, len) < 0)
                {
                    err_msg("writen error");
                }
            }
        }
        Pthread_mutex_unlock(&user_pool->lock);

        if (msg.ptr)
        {
            free(msg.ptr);
            msg.ptr = NULL;
        }
    }
    free(sendbuf);
    return NULL;
}

void fdclose(int fd, int pipefd)
{
    if (user_pool->owner == fd)
    {
        // room close
        user_pool->clear_room();
        printf("clear room\n");
        char cmd = 'E';
        if (writen(pipefd, &cmd, 1) < 1)
        {
            err_msg("writen error");
        }
    }
    else
    {
        uint32_t ip;
        Pthread_mutex_lock(&user_pool->lock);
        ip = user_pool->fdToIp[fd];
        FD_CLR(fd, &user_pool->fdset);
        user_pool->num--;
        user_pool->status[fd] = CLOSE;
        if (fd == maxfd)
            --maxfd;
        Pthread_mutex_unlock(&user_pool->lock);

        char cmd = 'Q';
        if (write(pipefd, &cmd, 1) < 1)
        {
            err_msg("write error");
        }
        MSG msg;
        memset(&msg, 0, sizeof(msg));
        msg.msgType = PARTNER_EXIT;
        msg.targetfd = -1;
        msg.ip = ip;
        Close(fd);
        send_queue.push_msg(msg);
    }
}
