//
// Created by CEN-KL on 2023/6/11.
//

#ifndef FXXKMEETING_MSG_H
#define FXXKMEETING_MSG_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include "netheader.h"

#define MAXSIZE 10000
#define MB (1024*1024)

enum STATUS {
    CLOSE = 0,
    ON = 1,
};

struct MSG {
    char *ptr;
    int len;
    int targetfd;
    MSG_TYPE msgType;
    uint32_t ip;
    Image_Format format;

    MSG() = default;
    MSG(MSG_TYPE msg_type, char *msg, int length, int fd): msgType(msg_type), ptr(msg), len(length), targetfd(fd) { }
};

struct SEND_QUEUE {
private:
    std::mutex m_mtx;
    std::condition_variable m_cv;

    std::queue<MSG> send_queue;

public:
    SEND_QUEUE() = default;
    SEND_QUEUE(const SEND_QUEUE&) = delete;
    SEND_QUEUE &operator=(const SEND_QUEUE&) = delete;

    void push_msg(MSG msg) {
        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_cv.wait(lock, [&] { return send_queue.size() < MAXSIZE; });
            send_queue.push(msg);
        }
        m_cv.notify_one();
    }

    MSG pop_msg() {
        MSG msg;
        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_cv.wait(lock, [&] { return !send_queue.empty(); });
            msg = send_queue.front();
            send_queue.pop();
        }
        return msg;
    }

    void clear() {
        std::unique_lock<std::mutex> lock(m_mtx);
        while (!send_queue.empty())
            send_queue.pop();
    }
};

#endif //FXXKMEETING_MSG_H
