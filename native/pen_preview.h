#pragma once
#include "annotations.h"

namespace callayers {
// Local strokes remain visible until the composed video contains each one.
class PenPreview {
    ULONG_PTR graphicsToken_=0;
    struct PendingStroke {uint64_t id,revision;};
    std::vector<PendingStroke> pending_;
public:
    PenPreview();
    ~PenPreview();
    PenPreview(const PenPreview&)=delete;
    PenPreview& operator=(const PenPreview&)=delete;
    void follow(uint64_t id,uint64_t revision);
    void cancel(uint64_t id);
    void clear(){pending_.clear();}
    bool paint(HDC dc,const std::vector<Annotation>& annotations,const RECT& preview,uint64_t renderedRevision);
};
}
