#include "output.h"
#include "video_format.h"
#include <linux/videodev2.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <QFile>
#include <cstring>

Output::~Output() { stop(); }

bool Output::start() {
    stop();
    stop_ = false;
    thread_ = std::thread([this] { run(); });
    return true;
}

void Output::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    active_ = false;
}

void Output::publish(const QImage& frame) {
    if (stop_ || frame.isNull()) return;
    std::lock_guard lock(mutex_);
    pending_ = frame;
    fresh_ = true;
}

QString Output::problem() const {
    std::lock_guard lock(mutex_);
    return problem_;
}

void Output::run() {
    int fd = ::open(OutputDevice, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        std::lock_guard lock(mutex_);
        problem_ = "The Callayers camera isn't available. Install Callayers again to set it up.";
        return;
    }
    v4l2_capability cap{};
    QFile nameFile("/sys/class/video4linux/video42/name");
    QString name;
    if (nameFile.open(QIODevice::ReadOnly)) name = QString::fromUtf8(nameFile.readAll()).trimmed();
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0 || QString::fromUtf8(reinterpret_cast<const char*>(cap.card)).trimmed() != "Callayers" || (!name.isEmpty() && name != "Callayers")) {
        ::close(fd);
        std::lock_guard lock(mutex_);
        problem_ = "The Callayers camera number is unavailable.";
        return;
    }
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    format.fmt.pix.width = OutputWidth;
    format.fmt.pix.height = OutputHeight;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    format.fmt.pix.field = V4L2_FIELD_NONE;
    format.fmt.pix.bytesperline = OutputWidth * 2;
    format.fmt.pix.sizeimage = OutputWidth * OutputHeight * 2;
    if (ioctl(fd, VIDIOC_S_FMT, &format) != 0 || format.fmt.pix.width != OutputWidth || format.fmt.pix.height != OutputHeight || format.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        ::close(fd);
        std::lock_guard lock(mutex_);
        problem_ = "The Callayers camera rejected the 1920×1080 picture.";
        return;
    }
    { std::lock_guard lock(mutex_); problem_.clear(); }
    active_ = true;
    std::vector<uint8_t> yuyv;
    while (!stop_) {
        QImage frame;
        {
            std::lock_guard lock(mutex_);
            if (fresh_) { frame = pending_; fresh_ = false; }
        }
        if (frame.isNull()) { usleep(10000); continue; }
        QImage bgra = frame.convertToFormat(QImage::Format_ARGB32);
        BgraToYuyv(bgra.constBits(), bgra.width(), bgra.height(), yuyv);
        const uint8_t* data = yuyv.data();
        size_t remaining = yuyv.size();
        while (remaining && !stop_) {
            ssize_t wrote = ::write(fd, data, remaining);
            if (wrote < 0) { if (errno == EAGAIN || errno == EINTR) { usleep(2000); continue; } break; }
            data += wrote;
            remaining -= size_t(wrote);
        }
    }
    active_ = false;
    ::close(fd);
}
