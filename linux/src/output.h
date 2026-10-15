#pragma once
#include <QImage>
#include <QString>
#include <atomic>
#include <mutex>
#include <thread>

class Output {
public:
    ~Output();
    bool start();
    void stop();
    void publish(const QImage& frame);
    bool active() const { return active_.load(); }
    QString problem() const;
private:
    void run();
    mutable std::mutex mutex_;
    QImage pending_;
    bool fresh_ = false;
    QString problem_;
    std::thread thread_;
    std::atomic<bool> stop_{true};
    std::atomic<bool> active_{false};
};
