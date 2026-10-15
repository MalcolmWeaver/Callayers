#include "capture.h"
#include <linux/videodev2.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <vector>

namespace {
struct Mapped { void* start = nullptr; size_t length = 0; };
struct Device {
    int fd = -1;
    std::vector<Mapped> maps;
    ~Device() {
        for (auto& map : maps) if (map.start && map.start != MAP_FAILED) munmap(map.start, map.length);
        if (fd >= 0) close(fd);
    }
};

QImage FromYuyv(const uint8_t* src, int width, int height) {
    QImage image(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) {
        auto row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; x += 2) {
            int y0 = src[0], u = src[1] - 128, y1 = src[2], v = src[3] - 128;
            auto pixel = [&](int yy) {
                int r = std::clamp(yy + ((359 * v) >> 8), 0, 255);
                int g = std::clamp(yy - ((88 * u + 183 * v) >> 8), 0, 255);
                int b = std::clamp(yy + ((454 * u) >> 8), 0, 255);
                return qRgb(r, g, b);
            };
            row[x] = pixel(y0);
            if (x + 1 < width) row[x + 1] = pixel(y1);
            src += 4;
        }
    }
    return image;
}
}

QVector<CameraChoice> EnumerateCameras() {
    QVector<CameraChoice> cameras;
    QDir dev("/dev");
    for (const auto& name : dev.entryList({"video*"}, QDir::System)) {
        if (!QRegularExpression("^video\\d+$").match(name).hasMatch()) continue;
        if (name.mid(5).toInt() == OutputNumber) continue;
        QString path = "/dev/" + name;
        int fd = ::open(path.toUtf8().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        v4l2_capability cap{};
        bool capture = ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
        ::close(fd);
        if (!capture) continue;
        uint32_t caps = cap.device_caps ? cap.device_caps : cap.capabilities;
        if (!(caps & V4L2_CAP_VIDEO_CAPTURE)) continue;
        QString card = QString::fromUtf8(reinterpret_cast<const char*>(cap.card)).trimmed();
        if (card == "Callayers") continue;
        QString identity = QFileInfo("/sys/class/video4linux/" + name + "/device").canonicalFilePath();
        if (identity.isEmpty()) identity = path;
        bool duplicate = false;
        for (const auto& camera : cameras) if (camera.identity == identity) duplicate = true;
        if (!duplicate) cameras.push_back({card.isEmpty() ? path : card, identity, path});
    }
    return cameras;
}

Capture::~Capture() { stop(); }

void Capture::start(const QString& identity) {
    stop();
    stop_ = false;
    error_ = CaptureError::None;
    thread_ = std::thread([this, identity] { run(identity); });
}

void Capture::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
    std::lock_guard lock(mutex_);
    latest_ = {};
}

QImage Capture::latest() const {
    std::lock_guard lock(mutex_);
    return latest_;
}

void Capture::run(QString identity) {
    QString path;
    for (const auto& camera : EnumerateCameras()) if (camera.identity == identity) path = camera.path;
    if (path.isEmpty()) { error_ = CaptureError::Missing; return; }
    Device device;
    device.fd = ::open(path.toUtf8().constData(), O_RDWR | O_NONBLOCK);
    if (device.fd < 0) {
        error_ = errno == EBUSY ? CaptureError::Busy : (errno == EACCES || errno == EPERM) ? CaptureError::Denied : CaptureError::Unreadable;
        return;
    }
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    uint32_t formats[] = {V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_MJPEG};
    bool chosen = false;
    for (auto pixel : formats) {
        format.fmt.pix = {};
        format.fmt.pix.width = 1280;
        format.fmt.pix.height = 720;
        format.fmt.pix.pixelformat = pixel;
        format.fmt.pix.field = V4L2_FIELD_NONE;
        if (ioctl(device.fd, VIDIOC_S_FMT, &format) == 0 && format.fmt.pix.pixelformat == pixel && format.fmt.pix.width > 0 && format.fmt.pix.height > 0) {
            chosen = true;
            break;
        }
    }
    if (!chosen && (ioctl(device.fd, VIDIOC_G_FMT, &format) != 0 || format.fmt.pix.width == 0)) {
        error_ = CaptureError::Unreadable;
        return;
    }
    v4l2_requestbuffers request{};
    request.count = 2;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (ioctl(device.fd, VIDIOC_REQBUFS, &request) != 0 || request.count < 1) { error_ = CaptureError::Unreadable; return; }
    device.maps.resize(request.count);
    for (uint32_t i = 0; i < request.count; ++i) {
        v4l2_buffer buffer{};
        buffer.type = request.type;
        buffer.memory = request.memory;
        buffer.index = i;
        if (ioctl(device.fd, VIDIOC_QUERYBUF, &buffer) != 0) { error_ = CaptureError::Unreadable; return; }
        device.maps[i].length = buffer.length;
        device.maps[i].start = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, device.fd, buffer.m.offset);
        if (device.maps[i].start == MAP_FAILED) { error_ = CaptureError::Unreadable; return; }
        if (ioctl(device.fd, VIDIOC_QBUF, &buffer) != 0) { error_ = CaptureError::Unreadable; return; }
    }
    int type = request.type;
    if (ioctl(device.fd, VIDIOC_STREAMON, &type) != 0) { error_ = CaptureError::Unreadable; return; }
    running_ = true;
    while (!stop_) {
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(device.fd, &ready);
        timeval wait{0, 200000};
        int selected = select(device.fd + 1, &ready, nullptr, nullptr, &wait);
        if (selected < 0) { if (errno == EINTR) continue; error_ = CaptureError::Unreadable; break; }
        if (!selected) continue;
        v4l2_buffer buffer{};
        buffer.type = request.type;
        buffer.memory = request.memory;
        if (ioctl(device.fd, VIDIOC_DQBUF, &buffer) != 0) { if (errno == EAGAIN) continue; error_ = CaptureError::Unreadable; break; }
        QImage image;
        auto bytes = static_cast<const uint8_t*>(device.maps[buffer.index].start);
        if (format.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) image = FromYuyv(bytes, format.fmt.pix.width, format.fmt.pix.height);
        else image = QImage::fromData(bytes, buffer.bytesused, "JPEG");
        if (!image.isNull()) {
            image = image.convertToFormat(QImage::Format_ARGB32);
            std::lock_guard lock(mutex_);
            latest_ = image;
            error_ = CaptureError::None;
        }
        ioctl(device.fd, VIDIOC_QBUF, &buffer);
    }
    ioctl(device.fd, VIDIOC_STREAMOFF, &type);
    running_ = false;
}
