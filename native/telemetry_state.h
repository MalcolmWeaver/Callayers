#pragma once
#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>
#include <cstdint>

namespace callayers {
using TelemetryValue=std::variant<bool,std::string>;
using TelemetryProperties=std::map<std::string,TelemetryValue>;
struct TelemetryEvent {std::string name;TelemetryProperties properties;};
struct TelemetryCard {std::string id,type;bool operator==(const TelemetryCard&)const=default;};
struct TelemetryPicture {uint64_t scene=0;std::vector<TelemetryCard> cards;};
struct TelemetryVideo {
    bool preview=false,connected=false,output=false;
    std::string app="none";
    uint64_t connection=0;
    bool operator==(const TelemetryVideo&)const=default;
};
class TelemetryState {
    std::function<void(TelemetryEvent)> emit_;
    bool active_=false,observed_=false;
    TelemetryVideo video_;
    std::map<std::string,std::string> previewCards_,outputCards_;
    void videoEvent(const TelemetryVideo& value);
    void cards(const std::vector<TelemetryCard>& values,const char* destination,std::map<std::string,std::string>& previous);
public:
    explicit TelemetryState(std::function<void(TelemetryEvent)> emit):emit_(std::move(emit)){}
    void begin();
    void observe(TelemetryVideo video,const std::vector<TelemetryCard>& preview,const std::vector<TelemetryCard>& output);
    void created(const std::string& kind,const std::string& cardId={});
    void end();
};
}
