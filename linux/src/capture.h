#pragma once
#include "video_format.h"
#include <QImage>
#include <QString>
#include <QVector>
#include <atomic>
#include <mutex>
#include <thread>

enum class CaptureError { None, Busy, Denied, Unreadable, Missing };

struct CameraChoice { QString name, identity, path; };

QVector<CameraChoice> EnumerateCameras();

class Capture {
public:
    ~Capture();
    void start(const QString& identity);
    void stop();
    QImage latest() const;
    CaptureError error() const { return error_.load(); }
    bool running() const { return running_.load(); }
private:
    void run(QString identity);
    mutable std::mutex mutex_;
    QImage latest_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::atomic<CaptureError> error_{CaptureError::None};
};
