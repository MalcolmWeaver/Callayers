#include "heartbeat.h"
#include <stdexcept>
#include <iostream>

void Expect(bool ok){if(!ok)throw std::runtime_error("Heartbeat timing failed");}
int main(){
    try{
        callayers::HeartbeatSchedule schedule;
        Expect(!schedule.Due(0));Expect(!schedule.Due(59999));Expect(schedule.Due(60000));
        Expect(!schedule.Due(60000));Expect(!schedule.Due(120000));Expect(!schedule.Due(359999));
        Expect(schedule.Due(360000));Expect(schedule.Due(660000));Expect(schedule.Due(960000));
        // Sleep across several slots: one snapshot on resume, still anchored
        // to process start, with no burst of invented historical snapshots.
        Expect(schedule.Due(2200000));Expect(!schedule.Due(2200001));
        Expect(!schedule.Due(2459999));Expect(schedule.Due(2460000));
        callayers::HeartbeatSchedule delayed;
        Expect(delayed.Due(61000));Expect(!delayed.Due(359999));Expect(delayed.Due(360000));
        std::cout<<"PASS: minutes 1/6/11/16, delayed ticks, and sleep without catch-up bursts\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
