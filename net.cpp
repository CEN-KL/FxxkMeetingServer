//
// Created by CEN-KL on 2023/6/11.
//
#include "unpthread.h"
#include <iostream>

uint32_t getpeerip(int fd)
{
    sockaddr_in peeraddr;
    socklen_t addrlen = sizeof(sockaddr);
    if(getpeername(fd, (sockaddr *)&peeraddr, &addrlen) < 0)
    {
        err_msg("getpeername error");
        return -1;
    }
    return peeraddr.sin_addr.s_addr;
}

char *Sock_ntop(char * str, int size ,const sockaddr * sa, socklen_t salen)
{
    switch (sa->sa_family)
    {
        case AF_INET:
        {
            auto *sin = (struct sockaddr_in *) sa; // 通用的sockaddr强转为IPV4的套接字地址结构
            if(inet_ntop(AF_INET, &sin->sin_addr, str, size) == NULL)  // n:网络, p:表示  ｜  将二进制的网络字节序的IP地址转换为一个点分十进制串
            {
                err_msg("inet_ntop error");
                return NULL;
            }
            if(ntohs(sin->sin_port) > 0)
            {
                snprintf(str + strlen(str), size  -  strlen(str), ":%d", ntohs(sin->sin_port));
            }
            return str;
        }
        case AF_INET6:
        {
            auto *sin = (struct sockaddr_in6 *) sa;
            if(inet_ntop(AF_INET6, &sin->sin6_addr, str, size) == NULL)
            {
                err_msg("inet_ntop error");
                return NULL;
            }
            if(ntohs(sin->sin6_port) > 0)
            {
                snprintf(str + strlen(str), size -  strlen(str), ":%d", ntohs(sin->sin6_port));
            }
            return str;
        }
        default:
            return (char *)"server error";
    }
    return NULL;
}

int Select(int nfds, fd_set * readfds, fd_set * writefds, fd_set * execpfds, struct timeval *timeout)
{
    int n;
    for(;;)
    {
        if((n = select(nfds, readfds, writefds, execpfds, timeout)) < 0)
        {
            if(errno == EINTR) continue;
            else err_quit("select error");
        }
        else break;
    }
    return n; //can return 0 on timeout
}

ssize_t	Readn(int fd, void * buf, size_t size)
{
    ssize_t lefttoread = size, hasread = 0;
    char *ptr = (char *)buf;
    while(lefttoread > 0)
    {
        if((hasread = read(fd, ptr, lefttoread)) < 0)
        {
            if(errno == EINTR)
            {
                hasread = 0;
            }
            else
            {
                return -1;
            }
        }
        else if(hasread == 0) //eof
        {
            break;
        }
        lefttoread -= hasread;
        ptr += hasread;
    }
    return size - lefttoread;
}

ssize_t writen(int fd, const void * buf, size_t n)
{
    ssize_t lefttowrite = n, haswrite = 0;
    char *ptr = (char *)buf;
    while(lefttowrite > 0)
    {
        if((haswrite = write(fd, ptr, lefttowrite)) < 0)
        {
            if(haswrite < 0 && errno == EINTR)
            {
                haswrite = 0;
            }
            else
            {
                return -1; //error
            }
        }
        lefttowrite -= haswrite;
        ptr += haswrite;
    }
    return n;
}

void Setsockopt(int fd, int level, int optname, const void * optval, socklen_t optlen)
{
    if(setsockopt(fd, level, optname, optval, optlen) < 0)
    {
        err_msg("setsockopt error");
    }
}

void Close(int fd)
{
    if(close(fd) < 0)
    {
        err_msg("Close error");
    }
}

void Listen(int fd, int backlog)
{
    if(listen(fd, backlog) < 0)
    {
        err_quit("listen error");
    }
}

int Tcp_listen(const char * host, const char * service, socklen_t * addrlen)
{
    int listenfd, n;
    const int on = 1;
    struct addrinfo hints, *res, *ressave;
    bzero(&hints, sizeof(struct addrinfo));

    hints.ai_flags = AI_PASSIVE; //设置了AI_PASSIVE标志，但没有指定主机名，那么return ipv6和ipv4通配地址
    hints.ai_family = AF_UNSPEC; //返回的是适用于指定主机名和服务名且适合任意协议族的地址
    hints.ai_socktype = SOCK_STREAM;

    char addr[MAXSOCKADDR];

    if((n = getaddrinfo(host, service, &hints, &res)) > 0)
    {
        err_quit("tcp listen error for %s %s: %s", host, service, gai_strerror(n));
    }
    ressave = res;
    do
    {
        listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(listenfd < 0)
        {
            continue; //error try again
        }
        Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if(bind(listenfd, res->ai_addr, res->ai_addrlen) == 0)
        {
            printf("server address: %s\n", Sock_ntop(addr, MAXSOCKADDR, res->ai_addr, res->ai_addrlen));
            break; //success
        }
        Close(listenfd);
    }while((res = res->ai_next) != NULL);
    freeaddrinfo(ressave); //free

    if(res == NULL)
    {
        err_quit("tcp listen error for %s: %s", host, service);
    }

    Listen(listenfd, LISTENQ);

    if(addrlen)
    {
        *addrlen = res->ai_addrlen;
    }

    return listenfd;
}

int Accept(int listenfd, SA * addr, socklen_t *addrlen)
{
    int n;
    for(;;)
    {
        if((n = accept(listenfd, addr, addrlen)) < 0)
        {
            if(errno == EINTR)
                continue;
            else
                err_quit("accept error");
        }
        else
        {
            return n;
        }
    }
}
void Socketpair(int family, int type, int protocol, int * sockfd)
{
    if(socketpair(family, type, protocol, sockfd) < 0)
    {
        err_quit("socketpair error");
    }
}

// 用于向fd传递文件描述符sendfd
ssize_t write_fd(int fd, void *ptr, size_t nbytes, int sendfd)
{
    struct msghdr msg;
    struct iovec iov[1];

    union{
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(int))];
    }control_un;
    struct cmsghdr *cmptr;

    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    cmptr = CMSG_FIRSTHDR(&msg);     //         获取第一个控制信息头部
    cmptr->cmsg_len = CMSG_LEN(sizeof(int)); // 控制信息头部长度
    cmptr->cmsg_level = SOL_SOCKET;          // 控制信息头部级别
    cmptr->cmsg_type = SCM_RIGHTS;           // 表示传递文件描述符
    *((int *) CMSG_DATA(cmptr)) = sendfd;    // 将 sendfd 的值存储在控制信息数据部分，即 CMSG_DATA(cmptr) 所指向的位置。

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    iov[0].iov_base = ptr;
    iov[0].iov_len = nbytes;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    return sendmsg(fd, &msg, 0);
}


ssize_t Write_fd(int fd, void *ptr, size_t nbytes, int sendfd)
{
    ssize_t n;
    if((n = write_fd(fd, ptr, nbytes, sendfd)) < 0)
    {
        err_quit("write fd error");
    }
    return n;
}


ssize_t read_fd(int fd, void *ptr, size_t nbytes, int *recvfd)
{
    struct msghdr msg;
    struct iovec iov[1];
    ssize_t n;

    union {
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(int))];
    }control_un;

    struct cmsghdr *cmptr;
    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    //-------------------------
    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    iov[0].iov_base = ptr;
    iov[0].iov_len = nbytes;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    if((n  = recvmsg(fd, &msg, MSG_WAITALL)) < 0)
    {
        std::cout << "recvmsg return neg: " << n << std::endl;
        return n;
    }

    if((cmptr = CMSG_FIRSTHDR(&msg)) != NULL && cmptr->cmsg_len == CMSG_LEN(sizeof(int)))
    {
        if(cmptr->cmsg_level != SOL_SOCKET)
        {
            err_msg("control level != SOL_SOCKET");
        }
        if(cmptr->cmsg_type != SCM_RIGHTS)
        {
            err_msg("control type != SCM_RIGHTS");
        }
        *recvfd = *((int *) CMSG_DATA(cmptr));
    }
    else
    {
        *recvfd = -1; // descroptor was not passed
    }

    return n;
}