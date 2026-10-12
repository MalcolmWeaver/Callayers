#pragma once
#include <cstdint>

namespace callayers {
// Elapsed process time: minute 1, 6, 11, ... . After sleep or a delayed UI
// tick, emit one current snapshot and skip missed slots instead of replaying them.
class HeartbeatSchedule {
    uint64_t next=60000;
public:
    bool Due(uint64_t uptime){
        if(uptime<next)return false;
        next=60000+((uptime-60000)/300000+1)*300000;
        return true;
    }
};
}
