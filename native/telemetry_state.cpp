#include "telemetry_state.h"
namespace callayers {
void TelemetryState::begin(){if(active_)return;active_=true;observed_=false;previewCards_.clear();outputCards_.clear();emit_({"session_started",{}});}
void TelemetryState::videoEvent(const TelemetryVideo& v){emit_({"video_state_changed",{{"preview_visible",v.preview},{"consumer_connected",v.connected},{"output_active",v.output},{"consumer_app",v.app}}});}
void TelemetryState::cards(const std::vector<TelemetryCard>& values,const char* destination,std::map<std::string,std::string>& previous){
    std::map<std::string,std::string> current;for(const auto& c:values)current.emplace(c.id,c.type);
    for(const auto& [id,type]:previous)if(!current.contains(id))emit_({"card_usage_changed",{{"card_id",id},{"card_type",type},{"destination",std::string(destination)},{"applied",false}}});
    for(const auto& [id,type]:current)if(!previous.contains(id))emit_({"card_usage_changed",{{"card_id",id},{"card_type",type},{"destination",std::string(destination)},{"applied",true}}});
    previous=std::move(current);
}
void TelemetryState::observe(TelemetryVideo v,const std::vector<TelemetryCard>& preview,const std::vector<TelemetryCard>& output){
    if(!active_)return;
    if(!v.connected){v.output=false;v.app="none";v.connection=0;}
    // A fast reconnect to the same app still closes the previous connection.
    if(observed_ && video_.connected && v.connected && video_.connection!=v.connection){
        cards({},"output",outputCards_);auto stopped=video_;stopped.connected=stopped.output=false;stopped.app="none";videoEvent(stopped);
    }
    if(!observed_ || v!=video_)videoEvent(v);
    cards(v.preview?preview:std::vector<TelemetryCard>{},"preview",previewCards_);
    cards(v.output?output:std::vector<TelemetryCard>{},"output",outputCards_);
    video_=std::move(v);observed_=true;
}
void TelemetryState::created(const std::string& kind,const std::string& cardId){if(!active_)return;TelemetryProperties p{{"kind",kind}};if(!cardId.empty())p.emplace("card_id",cardId);emit_({"content_created",std::move(p)});}
void TelemetryState::end(){if(!active_)return;auto stopped=video_;stopped.preview=stopped.output=false;observe(stopped, {}, {});active_=false;}
}
