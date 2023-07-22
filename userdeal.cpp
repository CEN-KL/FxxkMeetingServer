//
// Created by CEN-KL on 2023/6/11.
//

#include "room.h"
#include "msg.h"

std::mutex mlock;
extern socklen_t addrlen;
extern int listenfd;
extern int nprocesses;
extern Room *room;

void doWithUser(int);
void writeToClient(int, MSG);

// 监听并处理新的连接（创建或加入会议）
void thread_main() {
    int connfd;

    SA *cliaddr;  // 通用的套接字地址结构
    socklen_t clilen;
    cliaddr = (SA *) Calloc(1, addrlen);
    char buf[MAXSOCKADDR];
    while (true) {
        clilen = addrlen;
        {
            std::unique_lock<std::mutex> lock(mlock);
            connfd = Accept(listenfd, cliaddr, &clilen);
        }

        printf("connection from %s\n", Sock_ntop(buf, MAXSOCKADDR, cliaddr, clilen));

        doWithUser(connfd);
    }
}

void doWithUser(int connfd) {
    char head[15] = {0};
    while (true) {
        ssize_t ret = Readn(connfd, head, 11);  // 从connfd中读11个字节
        if (ret <= 0) {
            close(connfd);
            printf("%d close\n", connfd);
            return;
        } else if (ret < 11) {
            printf("data len too short\n");
        } else if (head[0] != '$') {
            printf("data format error\n");
        } else {
            // 正常读取
            MSG_TYPE msg_type;
            memcpy(&msg_type, head + 1, 2);
            msg_type = (MSG_TYPE) ntohs(msg_type);  // 转为主机字节序

            uint32_t ip;
            memcpy(&ip, head + 3, 4);
            ip = ntohl(ip);

            uint32_t datasize;
            memcpy(&datasize, head + 7, 4);
            datasize = ntohl(datasize);

            // 开始处理创建和加入会议的业务
            if (msg_type == CREATE_MEETING) {
                char tail;
                Readn(connfd, &tail, 1);
                if (datasize == 0 && tail == '#') {
                    char *c = (char *) &ip;
                    printf("create meeting  ip: %d.%d.%d.%d\n", (u_char)c[3], (u_char)c[2], (u_char)c[1], (u_char)c[0]);
                    if (room->navail <= 0) {
                        // 没有房间可用
                        MSG msg;
                        memset(&msg, 0, sizeof(msg));
                        msg.msgType = CREATE_MEETING_RESPONSE;
                        int roomNo = 0;
                        msg.ptr = (char *) malloc(sizeof(int));
                        memcpy(msg.ptr, &roomNo, sizeof (int));
                        msg.len = sizeof(int);
                        writeToClient(connfd, msg);
                    } else {
                        // 找一个空房间
                        int i;
                        std::unique_lock<std::mutex> lock(room->m_mtx);

                        for (i = 0; i < nprocesses; i++) {
                            if (room->pptr[i].child_status == 0) break;
                        }

                        if (i == nprocesses) {
                            // 没有房间可用
                            MSG msg;
                            memset(&msg, 0, sizeof(msg));
                            msg.msgType = CREATE_MEETING_RESPONSE;
                            int roomNo = 0;
                            msg.ptr = (char *) malloc(sizeof(int));
                            memcpy(msg.ptr, &roomNo, sizeof (int));
                            msg.len = sizeof(roomNo);
                            writeToClient(connfd, msg);
                        } else {
                            char cmd = 'C';
                            // 把客户端文件描述符connfd传递给房间进程
                            if (write_fd(room->pptr[i].child_pipefd, &cmd, 1, connfd) < 0) {
                                printf("write fd error");
                            } else {
                                // close(connfd);
                                printf("room %d is empty\n", room->pptr[i].child_pid);
                                room->pptr[i].child_status = 1;
                                room->pptr[i].total += 1;
                                room->navail -= 1;
                                Close(connfd);
                                lock.unlock();
                                return;
                            }
                        }
                    }
                } else {
                    printf("create meeting data format error\n");
                }
            }
            else if (msg_type == JOIN_MEETING) {
                uint32_t roomNo;
                // read data + #
                auto r = Readn(connfd, head, datasize + 1);
                if (r < datasize + 1) {
                    printf("data too short\n");
                } else {
                    if (head[datasize] == '#') {
                        memcpy(&roomNo, head, datasize);
                        roomNo = ntohl(roomNo);
                        bool ok = false;
                        int i;
                        for (i = 0; i < nprocesses; i++) {
                            if (room->pptr[i].child_pid == roomNo && room->pptr[i].child_status == 1) {
                                ok = true;
                                break;
                            }
                        }

                        MSG msg;
                        memset(&msg, 0, sizeof(msg));
                        msg.msgType = JOIN_MEETING_RESPONSE;
                        msg.len = sizeof(uint32_t);
                        if (ok) {
                            if (room->pptr[i].total >= 1024) {
                                // 会议室已满
                                msg.ptr = (char *) malloc(msg.len);
                                uint32_t full = -1;
                                memcpy(msg.ptr, &full, sizeof(uint32_t));
                                writeToClient(connfd, msg);
                            } else {
                                std::unique_lock<std::mutex> lock(room->m_mtx);
                                char cmd = 'J';
                                if (write_fd(room->pptr[i].child_pipefd, &cmd, 1, connfd) < 0) {
                                    err_msg("write fd: ");
                                } else {
                                    msg.ptr = (char *) malloc(msg.len);
                                    memcpy(msg.ptr, &roomNo, sizeof(uint32_t));
                                    writeToClient(connfd, msg);
                                    room->pptr[i].total += 1;
                                    lock.unlock();
                                    Close(connfd);
                                    return;
                                }
                            }
                        } else {
                            msg.ptr = (char *) malloc(msg.len);
                            uint32_t fail = 0;
                            memcpy(msg.ptr, &fail, sizeof(uint32_t));
                            writeToClient(connfd, msg);
                        }
                    } else {
                        printf("format error\n");
                    }
                }
            }
            else {
                printf("data format error\n");
            }
        }
    }
}

// 直接写到客户端的socket
void writeToClient(int fd, MSG msg) {
    char *buf = (char *) malloc(100);
    memset(buf, 0, 100);
    int bytes_to_write = 0;
    buf[bytes_to_write++] = '$';

    uint16_t type = msg.msgType;
    type = htons(type);  // 转为网络字节序
    memcpy(buf + bytes_to_write, &type, sizeof(uint16_t));
    bytes_to_write += 2;

    bytes_to_write += 4; // skip ip

    uint32_t size = msg.len;
    size = htonl(size);
    memcpy(buf + bytes_to_write, &size, sizeof(uint32_t));
    bytes_to_write += 4;

    memcpy(buf + bytes_to_write, msg.ptr, msg.len); // 写具体数据
    bytes_to_write += msg.len;

    buf[bytes_to_write++] = '#';

    if (writen(fd, buf, bytes_to_write) < bytes_to_write)
        printf("write fail");

    if(msg.ptr) {
        free(msg.ptr);
        msg.ptr = NULL;
    }
    free(buf);
}