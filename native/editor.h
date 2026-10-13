#pragma once
#include "common.h"
#include "image_asset.h"
#include "card_assets.h"
#include <filesystem>
#include <cmath>
#include <set>
#include <map>
#include <functional>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace callayers {
struct TextElement {
    std::wstring id,text=L"Type your text",family=L"Segoe UI";
    double x=.1,y=.15,width=.5,height=.2,size=56;
    uint32_t color=0xffffff;
    bool bold=false;
    int alignment=0;
    int order=0;
    bool operator==(const TextElement&)const=default;
};
struct CardElement {
    std::wstring id,html,name=L"Card";
    double x=.1,y=.15,width=.42,height=.45;
    std::vector<CardAsset> assets;
    std::optional<CardUsage> usage;
    std::wstring state=L"{}"; // Persisted JSON object; HTML and assets remain separate.
    double renderScale=1; // Preview-only scale; never serialized.
    bool operator==(const CardElement&)const=default;
};
inline CardElement MoveCard(const CardElement& start,double dx,double dy){
    auto c=start;c.x=std::clamp(c.x+dx,0.,1.-c.width);c.y=std::clamp(c.y+dy,0.,1.-c.height);return c;
}
inline CardElement ResizeCard(const CardElement& start,int handle,double dx,double dy){
    auto c=start;bool left=handle==0||handle==3||handle==4,top=handle==0||handle==1;
    double edge=std::clamp((left?start.x:start.x+start.width)+dx,left?0.:start.x+.08,left?start.x+start.width-.08:1.);
    c.x=left?edge:start.x;c.width=left?start.x+start.width-edge:edge-start.x;
    if(handle<4){double y=std::clamp((top?start.y:start.y+start.height)+dy,top?0.:start.y+.08,top?start.y+start.height-.08:1.);c.y=top?y:start.y;c.height=top?start.y+start.height-y:y-start.y;}
    return c;
}
struct InkPoint {
    double x=0,y=0;
    bool operator==(const InkPoint&)const=default;
};
struct ImageElement {
    std::wstring id,name=L"Image";
    std::shared_ptr<const ImageAsset> asset;
    double x=.1,y=.15,width=.4,height=.4;
    int order=0;
    bool operator==(const ImageElement& other)const{
        return id==other.id&&name==other.name&&x==other.x&&y==other.y&&width==other.width&&height==other.height&&order==other.order&&
            (asset==other.asset||(asset&&other.asset&&asset->png==other.asset->png));
    }
};
inline ImageElement MoveImage(const ImageElement& start,double dx,double dy){auto image=start;image.x=std::clamp(start.x+dx,0.,1.-start.width);image.y=std::clamp(start.y+dy,0.,1.-start.height);return image;}
inline ImageElement ResizeImage(const ImageElement& start,int handle,double dx,double dy){
    auto image=start;bool left=handle==0||handle==3||handle==4,top=handle==0||handle==1;
    double sx=(start.width+(left?-dx:dx))/start.width,sy=(start.height+(top?-dy:dy))/start.height;
    double scale=handle>=4||std::abs(sx-1)>std::abs(sy-1)?sx:sy;
    double upper=std::min((left?start.x+start.width:1.-start.x)/start.width,(top?start.y+start.height:1.-start.y)/start.height);
    scale=std::clamp(scale,std::min(.02/std::max(start.width,start.height),upper),upper);
    image.width=start.width*scale;image.height=start.height*scale;if(left)image.x=start.x+start.width-image.width;if(top)image.y=start.y+start.height-image.height;return image;
}
struct InkStroke {
    std::wstring id;
    std::vector<InkPoint> points;
    uint32_t color=0xff0000;
    double width=6;
    bool filled=false;
    bool operator==(const InkStroke&)const=default;
};
inline constexpr size_t MaxInkStrokes=512,MaxStrokePoints=4096,MaxInkPoints=65536;
inline double PointSegmentDistance(InkPoint p,InkPoint a,InkPoint b){
    double dx=(b.x-a.x)*Width,dy=(b.y-a.y)*Height,px=(p.x-a.x)*Width,py=(p.y-a.y)*Height;
    double t=dx*dx+dy*dy>0?std::clamp((px*dx+py*dy)/(dx*dx+dy*dy),0.,1.):0.;return std::hypot(px-t*dx,py-t*dy);
}
inline bool InkHit(const InkStroke& stroke,InkPoint from,InkPoint to,double radius){
    if(stroke.filled&&stroke.points.size()>=3){
        auto inside=[&](InkPoint p){bool hit=false;for(size_t i=0,j=stroke.points.size()-1;i<stroke.points.size();j=i++){
            auto a=stroke.points[i],b=stroke.points[j];if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)hit=!hit;
        }return hit;};if(inside(from)||inside(to))return true;
    }
    radius+=stroke.width/2;
    for(size_t i=0;i<stroke.points.size();++i){auto a=stroke.points[i],b=stroke.points[i?i-1:i];
        if(std::min({PointSegmentDistance(a,from,to),PointSegmentDistance(b,from,to),PointSegmentDistance(from,a,b),PointSegmentDistance(to,a,b)})<=radius)return true;
        auto cross=[](InkPoint a,InkPoint b,InkPoint c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
        if(cross(a,b,from)*cross(a,b,to)<0&&cross(from,to,a)*cross(from,to,b)<0)return true;
    }
    return false;
}
struct Scene {
    std::wstring title=L"Untitled";
    std::vector<TextElement> texts;
    std::vector<CardElement> cards;
    std::vector<ImageElement> images;
    std::vector<InkStroke> ink;
    bool operator==(const Scene&)const=default;
};
inline size_t ElementCount(const Scene& scene){return scene.texts.size()+scene.cards.size()+scene.images.size();}
inline int NextForegroundOrder(const Scene& scene){int order=0;for(const auto& t:scene.texts)order=std::max(order,t.order+1);for(const auto& image:scene.images)order=std::max(order,image.order+1);return order;}
inline std::wstring NewId(){GUID id;Check(CoCreateGuid(&id));wchar_t text[40];StringFromGUID2(id,text,40);return text;}
// Only action metadata crosses this callback; annotation content stays in the editor.
struct AnnotationChange {
    const wchar_t* action;
    const wchar_t* type;
    const wchar_t* source;
    size_t count;
};
class Document {
    std::vector<Scene> past,future;
    std::optional<std::vector<TextElement>> textAnnotationSession;
    Scene saved;
    uint64_t epoch=1;
    std::map<std::wstring,uint64_t> contentRevisions;
    void TrackAnnotations(const Scene& before,const wchar_t* source,bool cleared=false) noexcept {
        if(!AnnotationsChanged)return;
        try{
            auto track=[&](const auto& old,const auto& current,const wchar_t* type){
                size_t created=0,edited=0,deleted=0;
                for(const auto& item:current){
                    auto previous=std::find_if(old.begin(),old.end(),[&](const auto& value){return value.id==item.id;});
                    if(previous==old.end())++created;else if(*previous!=item)++edited;
                }
                for(const auto& item:old)if(std::none_of(current.begin(),current.end(),[&](const auto& value){return value.id==item.id;}))++deleted;
                if(created)AnnotationsChanged({L"created",type,source,created});
                if(edited)AnnotationsChanged({L"edited",type,source,edited});
                if(deleted)AnnotationsChanged({cleared?L"cleared":L"deleted",type,source,deleted});
            };
            if(!textAnnotationSession)track(before.texts,scene.texts,L"text");
            track(before.ink,scene.ink,L"drawing");
        }catch(...){} // Analytics failure must never interrupt an edit or undo.
    }
    void TrackContent(const Scene& before){
        std::set<std::wstring> changed;
        for(const auto& old:before.cards){
            auto now=std::find_if(scene.cards.begin(),scene.cards.end(),[&](const auto& c){return c.id==old.id;});
            if(now==scene.cards.end()||now->html!=old.html||now->name!=old.name||now->assets!=old.assets)changed.insert(old.id);
        }
        for(const auto& now:scene.cards)if(std::none_of(before.cards.begin(),before.cards.end(),[&](const auto& c){return c.id==now.id;}))changed.insert(now.id);
        for(const auto& id:changed)++contentRevisions[id];
    }
public:
    std::function<void(const AnnotationChange&)> AnnotationsChanged;
    Scene scene;
    void BeginTextAnnotationSession(const Scene& before){
        if(!textAnnotationSession)textAnnotationSession=before.texts;
    }
    void EndTextAnnotationSession(bool committed){
        if(!textAnnotationSession)return;
        Scene before;before.texts=std::move(*textAnnotationSession);textAnnotationSession.reset();
        // Drawings have their own gesture events and must not be counted twice.
        before.ink=scene.ink;
        if(committed)TrackAnnotations(before,L"direct");
    }
    bool Dirty()const{auto current=scene,baseline=saved;current.texts.clear();current.ink.clear();baseline.texts.clear();baseline.ink.clear();return current!=baseline;}
    void ClearAnnotations(){
        auto before=scene;
        auto clear=[](Scene& value){value.texts.clear();value.ink.clear();};
        clear(scene);clear(saved);for(auto& value:past)clear(value);for(auto& value:future)clear(value);
        past.erase(std::unique(past.begin(),past.end()),past.end());future.erase(std::unique(future.begin(),future.end()),future.end());
        while(!past.empty()&&past.back()==scene)past.pop_back();while(!future.empty()&&future.back()==scene)future.pop_back();
        TrackAnnotations(before,L"card_switch",true);
    }
    bool CanUndo()const{return !past.empty();}
    bool CanRedo()const{return !future.empty();}
    void Commit(const Scene& before,bool cleared=false){
        if(scene==before)return;
        TrackContent(before);past.push_back(before);if(past.size()>100)past.erase(past.begin());future.clear();
        TrackAnnotations(before,L"direct",cleared);
    }
    void Undo(){if(past.empty())return;future.push_back(scene);scene=past.back();past.pop_back();TrackContent(future.back());TrackAnnotations(future.back(),L"undo");}
    void Redo(){if(future.empty())return;past.push_back(scene);scene=future.back();future.pop_back();TrackContent(past.back());TrackAnnotations(past.back(),L"redo");}
    void Loaded(Scene next){scene=std::move(next);saved=scene;past.clear();future.clear();++epoch;contentRevisions.clear();}
    void Saved(){saved=scene;}
    uint64_t Epoch()const{return epoch;}
    uint64_t ContentRevision(const std::wstring& id)const{auto it=contentRevisions.find(id);return it==contentRevisions.end()?0:it->second;}
};
struct CardRequestTarget {
    uint64_t epoch=0,revision=0;
    bool creating=true;
    CardElement card;
};
inline CardRequestTarget CaptureCardTarget(const Document& document,const std::wstring& selectedId={}){
    CardRequestTarget target;target.epoch=document.Epoch();target.card.id=NewId();
    for(const auto& c:document.scene.cards)if(c.id==selectedId){target.creating=false;target.card=c;target.revision=document.ContentRevision(c.id);break;}
    return target;
}
inline bool CardTargetCurrent(const Document& document,const CardRequestTarget& target){
    if(document.Epoch()!=target.epoch)return false;
    if(target.creating)return document.scene.cards.size()<12&&ElementCount(document.scene)<100&&std::none_of(document.scene.cards.begin(),document.scene.cards.end(),[&](const auto& c){return c.id==target.card.id;});
    return document.ContentRevision(target.card.id)==target.revision&&std::any_of(document.scene.cards.begin(),document.scene.cards.end(),[&](const auto& c){return c.id==target.card.id&&c.html==target.card.html;});
}
inline bool ApplyGeneratedCard(Document& document,const CardRequestTarget& target,const std::wstring& html,const std::wstring& name,const std::vector<CardAsset>& assets={},const std::optional<CardUsage>& usage={}){
    if(!CardTargetCurrent(document,target))return false;
    auto selectedAssets=SelectCardAssets(html,assets);
    size_t mediaChars=0;for(const auto& image:document.scene.images)if(image.asset)mediaChars+=image.asset->png.size();
    for(const auto& card:document.scene.cards)if(card.id!=target.card.id)for(const auto& asset:card.assets)mediaChars+=asset.dataUri->size();
    for(const auto& asset:selectedAssets)mediaChars+=asset.dataUri->size();
    if(mediaChars>MaxSceneImageChars)throw std::runtime_error("This deck has reached its embedded image limit.");
    auto before=document.scene;
    if(target.creating){auto card=target.card;card.html=html;card.name=name;card.assets=std::move(selectedAssets);card.usage=usage;document.scene.cards.push_back(std::move(card));}
    else for(auto& card:document.scene.cards)if(card.id==target.card.id){card.html=html;card.name=name;card.assets=std::move(selectedAssets);card.usage=usage;break;}
    document.Commit(before);return true;
}
// Dragging uses a fixed starting element, so every move is absolute rather than cumulative.
inline TextElement MoveText(const TextElement& start,double dx,double dy){
    auto t=start;t.x=std::clamp(start.x+dx,0.,1.-t.width);t.y=std::clamp(start.y+dy,0.,1.-t.height);return t;
}
// Handles: 0 TL, 1 TR, 2 BR, 3 BL, 4 left side, 5 right side.
inline TextElement ResizeText(const TextElement& start,int handle,double dx,double dy){
    auto t=start;
    bool left=handle==0||handle==3||handle==4,top=handle==0||handle==1;
    if(handle>=4){
        double edge=left?std::clamp(start.x+dx,0.,start.x+start.width-.05):std::clamp(start.x+start.width+dx,start.x+.05,1.);
        t.x=left?edge:start.x;t.width=left?start.x+start.width-edge:edge-start.x;return t;
    }
    double sx=(start.width+(left?-dx:dx))/start.width;
    double sy=(start.height+(top?-dy:dy))/start.height;
    double scale=std::abs(sx-1)>std::abs(sy-1)?sx:sy;
    double maxX=(left?start.x+start.width:1.-start.x)/start.width;
    double maxY=(top?start.y+start.height:1.-start.y)/start.height;
    double lower=std::max({.05/start.width,.01/start.height,8./start.size});
    double upper=std::min({maxX,maxY,300./start.size});
    scale=std::clamp(scale,std::min(lower,upper),upper);
    t.width=start.width*scale;t.height=start.height*scale;t.size=start.size*scale;
    if(left)t.x=start.x+start.width-t.width;if(top)t.y=start.y+start.height-t.height;
    return t;
}
inline std::string EncodeScene(const Scene& scene){
    using namespace winrt::Windows::Data::Json;
    JsonObject root;root.SetNamedValue(L"format",JsonValue::CreateStringValue(L"callayers"));
    bool mediaCards=std::any_of(scene.cards.begin(),scene.cards.end(),[](const auto& c){return !c.assets.empty()||c.usage.has_value();});
    bool stateCards=std::any_of(scene.cards.begin(),scene.cards.end(),[](const auto& c){return c.state!=L"{}";});
    root.SetNamedValue(L"version",JsonValue::CreateNumberValue(stateCards?6:mediaCards?5:!scene.images.empty()?4:!scene.ink.empty()?3:scene.cards.empty()?1:2));root.SetNamedValue(L"title",JsonValue::CreateStringValue(scene.title));
    JsonArray elements;
    for(const auto& t:scene.texts){
        JsonObject o;
        o.SetNamedValue(L"type",JsonValue::CreateStringValue(L"text"));
        o.SetNamedValue(L"id",JsonValue::CreateStringValue(t.id));o.SetNamedValue(L"text",JsonValue::CreateStringValue(t.text));
        o.SetNamedValue(L"family",JsonValue::CreateStringValue(t.family));
        o.SetNamedValue(L"x",JsonValue::CreateNumberValue(t.x));o.SetNamedValue(L"y",JsonValue::CreateNumberValue(t.y));
        o.SetNamedValue(L"width",JsonValue::CreateNumberValue(t.width));o.SetNamedValue(L"height",JsonValue::CreateNumberValue(t.height));
        o.SetNamedValue(L"size",JsonValue::CreateNumberValue(t.size));o.SetNamedValue(L"color",JsonValue::CreateNumberValue(t.color));
        o.SetNamedValue(L"bold",JsonValue::CreateBooleanValue(t.bold));o.SetNamedValue(L"alignment",JsonValue::CreateNumberValue(t.alignment));
        o.SetNamedValue(L"order",JsonValue::CreateNumberValue(t.order));
        elements.Append(o);
    }
    for(const auto& c:scene.cards){
        JsonObject o;o.SetNamedValue(L"type",JsonValue::CreateStringValue(L"card"));
        o.SetNamedValue(L"id",JsonValue::CreateStringValue(c.id));o.SetNamedValue(L"html",JsonValue::CreateStringValue(c.html));o.SetNamedValue(L"name",JsonValue::CreateStringValue(c.name));
        o.SetNamedValue(L"x",JsonValue::CreateNumberValue(c.x));o.SetNamedValue(L"y",JsonValue::CreateNumberValue(c.y));
        o.SetNamedValue(L"width",JsonValue::CreateNumberValue(c.width));o.SetNamedValue(L"height",JsonValue::CreateNumberValue(c.height));
        if(c.state.size()>65536)throw std::runtime_error("Card state is too large.");
        o.SetNamedValue(L"state",JsonObject::Parse(c.state));
        ValidateCardAssets(c.assets);
        if(!c.assets.empty()){
            JsonArray assets;for(const auto& a:c.assets){JsonObject value;
                value.SetNamedValue(L"id",JsonValue::CreateStringValue(a.id));value.SetNamedValue(L"description",JsonValue::CreateStringValue(a.description));value.SetNamedValue(L"source",JsonValue::CreateStringValue(a.sourceUrl));
                value.SetNamedValue(L"width",JsonValue::CreateNumberValue(a.width));value.SetNamedValue(L"height",JsonValue::CreateNumberValue(a.height));value.SetNamedValue(L"data",JsonValue::CreateStringValue(*a.dataUri));assets.Append(value);
            }o.SetNamedValue(L"assets",assets);
        }
        if(c.usage){const auto& u=*c.usage;JsonObject value;
            value.SetNamedValue(L"model",JsonValue::CreateStringValue(u.model));
            value.SetNamedValue(L"input",JsonValue::CreateNumberValue(static_cast<double>(u.input)));value.SetNamedValue(L"output",JsonValue::CreateNumberValue(static_cast<double>(u.output)));
            value.SetNamedValue(L"cached",JsonValue::CreateNumberValue(static_cast<double>(u.cached)));value.SetNamedValue(L"cache_write",JsonValue::CreateNumberValue(static_cast<double>(u.cacheWrite)));value.SetNamedValue(L"reasoning",JsonValue::CreateNumberValue(static_cast<double>(u.reasoning)));
            if(u.estimatedUsd)value.SetNamedValue(L"estimated_usd",JsonValue::CreateNumberValue(*u.estimatedUsd));o.SetNamedValue(L"usage",value);
        }elements.Append(o);
    }
    for(const auto& image:scene.images){
        if(!image.asset)throw std::runtime_error("Image data is missing.");JsonObject o;o.SetNamedValue(L"type",JsonValue::CreateStringValue(L"image"));
        o.SetNamedValue(L"id",JsonValue::CreateStringValue(image.id));o.SetNamedValue(L"name",JsonValue::CreateStringValue(image.name));o.SetNamedValue(L"png",JsonValue::CreateStringValue(image.asset->png));
        o.SetNamedValue(L"x",JsonValue::CreateNumberValue(image.x));o.SetNamedValue(L"y",JsonValue::CreateNumberValue(image.y));o.SetNamedValue(L"width",JsonValue::CreateNumberValue(image.width));o.SetNamedValue(L"height",JsonValue::CreateNumberValue(image.height));o.SetNamedValue(L"order",JsonValue::CreateNumberValue(image.order));elements.Append(o);
    }
    for(const auto& stroke:scene.ink){
        JsonObject o;o.SetNamedValue(L"type",JsonValue::CreateStringValue(L"ink"));o.SetNamedValue(L"id",JsonValue::CreateStringValue(stroke.id));
        o.SetNamedValue(L"filled",JsonValue::CreateBooleanValue(stroke.filled));o.SetNamedValue(L"color",JsonValue::CreateNumberValue(stroke.color));o.SetNamedValue(L"width",JsonValue::CreateNumberValue(stroke.width));
        JsonArray points;for(auto p:stroke.points){JsonArray pair;pair.Append(JsonValue::CreateNumberValue(p.x));pair.Append(JsonValue::CreateNumberValue(p.y));points.Append(pair);}
        o.SetNamedValue(L"points",points);elements.Append(o);
    }
    JsonObject shot;shot.SetNamedValue(L"elements",elements);JsonArray scenes;scenes.Append(shot);root.SetNamedValue(L"scenes",scenes);
    return winrt::to_string(root.Stringify());
}
inline Scene DecodeScene(const std::string& bytes){
    using namespace winrt::Windows::Data::Json;
    if(bytes.size()>MaxDeckBytes)throw std::runtime_error("This deck is too large to open.");
    auto root=JsonObject::Parse(winrt::to_hstring(bytes));
    if(root.GetNamedString(L"format")!=L"callayers"||(root.GetNamedNumber(L"version")!=1&&root.GetNamedNumber(L"version")!=2&&root.GetNamedNumber(L"version")!=3&&root.GetNamedNumber(L"version")!=4&&root.GetNamedNumber(L"version")!=5&&root.GetNamedNumber(L"version")!=6))
        throw std::runtime_error("Unsupported Callayers file version.");
    auto scenes=root.GetNamedArray(L"scenes");
    if(scenes.Size()!=1)throw std::runtime_error("This editor currently supports one scene per file.");
    Scene scene;scene.title=root.GetNamedString(L"title");
    auto items=scenes.GetObjectAt(0).GetNamedArray(L"elements");
    if(items.Size()>100+MaxInkStrokes)throw std::runtime_error("This scene exceeds the element limit.");
    size_t totalPoints=0;
    size_t imageChars=0;std::unique_ptr<ImageCodec> imageCodec;
    auto readOrder=[](const JsonObject& o){double order=o.GetNamedNumber(L"order",0);if(!std::isfinite(order)||std::abs(order)>1000000||order!=std::floor(order))throw std::runtime_error("Invalid layer order.");return static_cast<int>(order);};
    std::set<std::wstring> ids;
    for(const auto& item:items){
        auto o=item.GetObject();TextElement t;
        if(o.GetNamedString(L"type")==L"ink"){
            InkStroke stroke;stroke.id=o.GetNamedString(L"id");stroke.width=o.GetNamedNumber(L"width");double color=o.GetNamedNumber(L"color");auto points=o.GetNamedArray(L"points");
            totalPoints+=points.Size();
            if(stroke.id.empty()||!ids.insert(stroke.id).second||!std::isfinite(stroke.width)||stroke.width<1||stroke.width>40||
                !std::isfinite(color)||color<0||color>0xffffff||color!=std::floor(color)||points.Size()==0||points.Size()>MaxStrokePoints||totalPoints>MaxInkPoints||scene.ink.size()>=MaxInkStrokes)
                throw std::runtime_error("Invalid drawing or drawing limit exceeded.");
            stroke.filled=o.GetNamedBoolean(L"filled",false);stroke.color=static_cast<uint32_t>(color);
            for(const auto& value:points){auto pair=value.GetArray();if(pair.Size()!=2)throw std::runtime_error("Invalid ink point.");InkPoint p{pair.GetNumberAt(0),pair.GetNumberAt(1)};
                if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<0||p.y<0||p.x>1||p.y>1)throw std::runtime_error("Ink is outside the stage.");stroke.points.push_back(p);}
            scene.ink.push_back(std::move(stroke));continue;
        }
        if(ElementCount(scene)>=100)throw std::runtime_error("This scene exceeds the 100-element limit.");
        if(o.GetNamedString(L"type")==L"image"){
            ImageElement image;image.id=o.GetNamedString(L"id");image.name=o.GetNamedString(L"name");image.order=readOrder(o);
            image.x=o.GetNamedNumber(L"x");image.y=o.GetNamedNumber(L"y");image.width=o.GetNamedNumber(L"width");image.height=o.GetNamedNumber(L"height");auto png=o.GetNamedString(L"png");imageChars+=png.size();
            if(image.id.empty()||!ids.insert(image.id).second||image.name.empty()||image.name.size()>260||scene.images.size()>=32||imageChars>MaxSceneImageChars||
                !std::isfinite(image.x)||!std::isfinite(image.y)||!std::isfinite(image.width)||!std::isfinite(image.height)||image.x<0||image.y<0||image.width<=0||image.height<=0||image.x+image.width>1.000001||image.y+image.height>1.000001)throw std::runtime_error("Invalid image geometry or image limit exceeded.");
            if(!imageCodec)imageCodec=std::make_unique<ImageCodec>();image.asset=imageCodec->LoadEmbedded(std::wstring(png));scene.images.push_back(std::move(image));continue;
        }
        if(o.GetNamedString(L"type")==L"card"){
            CardElement c;c.id=o.GetNamedString(L"id");c.html=o.GetNamedString(L"html");c.name=o.GetNamedString(L"name",L"Card");
            if(o.HasKey(L"state")){c.state=o.GetNamedObject(L"state").Stringify();if(c.state.size()>65536)throw std::runtime_error("Card state is too large.");}
            if(o.HasKey(L"assets")){
                for(const auto& assetValue:o.GetNamedArray(L"assets")){auto value=assetValue.GetObject();CardAsset a;
                    a.id=value.GetNamedString(L"id");a.description=value.GetNamedString(L"description");a.sourceUrl=value.GetNamedString(L"source");
                    double w=value.GetNamedNumber(L"width"),h=value.GetNamedNumber(L"height");if(!std::isfinite(w)||!std::isfinite(h)||w<0||h<0||w>4096||h>4096||w!=std::floor(w)||h!=std::floor(h))throw std::runtime_error("Invalid card image dimensions.");
                    a.width=static_cast<UINT>(w);a.height=static_cast<UINT>(h);a.dataUri=std::make_shared<const std::wstring>(value.GetNamedString(L"data"));imageChars+=a.dataUri->size();
                    if(imageChars>MaxSceneImageChars||c.assets.size()>=MaxCardAssets)throw std::runtime_error("Too many embedded card images.");c.assets.push_back(std::move(a));
                }ValidateCardAssets(c.assets);
            }
            SelectCardAssets(c.html,c.assets);
            if(o.HasKey(L"usage")){
                auto value=o.GetNamedObject(L"usage");CardUsage u;u.model=value.GetNamedString(L"model");if(u.model.size()>100)throw std::runtime_error("Invalid usage model.");
                auto count=[&](const wchar_t* key){double n=value.GetNamedNumber(key,0);if(!std::isfinite(n)||n<0||n>1000000000||n!=std::floor(n))throw std::runtime_error("Invalid token usage.");return static_cast<uint64_t>(n);};
                u.input=count(L"input");u.output=count(L"output");u.cached=count(L"cached");u.cacheWrite=count(L"cache_write");u.reasoning=count(L"reasoning");
                if(u.cached+u.cacheWrite>u.input||u.reasoning>u.output)throw std::runtime_error("Invalid token usage totals.");
                if(value.HasKey(L"estimated_usd")){double cost=value.GetNamedNumber(L"estimated_usd");if(!std::isfinite(cost)||cost<0||cost>1000000)throw std::runtime_error("Invalid cost estimate.");u.estimatedUsd=cost;}c.usage=u;
            }
            c.x=o.GetNamedNumber(L"x");c.y=o.GetNamedNumber(L"y");c.width=o.GetNamedNumber(L"width");c.height=o.GetNamedNumber(L"height");
            if(c.id.empty()||!ids.insert(c.id).second||c.html.size()>200000||c.name.empty()||c.name.size()>100||scene.cards.size()>=12||
                !std::isfinite(c.x)||!std::isfinite(c.y)||!std::isfinite(c.width)||!std::isfinite(c.height)||
                c.x<0||c.y<0||c.width<.08||c.height<.08||c.x+c.width>1.000001||c.y+c.height>1.000001)
                throw std::runtime_error("Invalid card geometry or content.");
            scene.cards.push_back(std::move(c));continue;
        }
        if(o.GetNamedString(L"type")!=L"text")throw std::runtime_error("This file contains elements not supported by this editor yet.");
        t.id=o.GetNamedString(L"id");t.text=o.GetNamedString(L"text");t.family=o.GetNamedString(L"family");
        t.x=o.GetNamedNumber(L"x");t.y=o.GetNamedNumber(L"y");t.width=o.GetNamedNumber(L"width");t.height=o.GetNamedNumber(L"height");
        t.size=o.GetNamedNumber(L"size");t.bold=o.GetNamedBoolean(L"bold");t.order=readOrder(o);
        double color=o.GetNamedNumber(L"color"),alignment=o.GetNamedNumber(L"alignment");
        if(!std::isfinite(color)||color<0||color>0xffffff||color!=std::floor(color)||
           !std::isfinite(alignment)||alignment<0||alignment>2||alignment!=std::floor(alignment))throw std::runtime_error("Invalid text style.");
        t.color=static_cast<uint32_t>(color);t.alignment=static_cast<int>(alignment);
        if(t.id.empty()||!ids.insert(t.id).second||t.text.size()>20000||t.family.empty()||t.family.size()>128||
           !std::isfinite(t.x)||!std::isfinite(t.y)||!std::isfinite(t.width)||!std::isfinite(t.height)||!std::isfinite(t.size)||
           t.x<0||t.y<0||t.width<.01||t.height<.01||t.x+t.width>1.000001||t.y+t.height>1.000001||t.size<8||t.size>300)
            throw std::runtime_error("Invalid text geometry or content.");
        scene.texts.push_back(std::move(t));
    }
    return scene;
}
inline void SaveScene(const std::filesystem::path& path,const Scene& scene){
    auto bytes=EncodeScene(scene);
    if(bytes.size()>MaxDeckBytes)throw std::runtime_error("This deck is too large to save. Remove an image or use smaller images.");
    auto temporary=path;temporary+=L"."+NewId()+L".tmp";
    try{
        Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(!file)throw HRESULT_FROM_WIN32(GetLastError());DWORD written=0;
        if(!WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||written!=bytes.size())throw HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        if(!FlushFileBuffers(file.value))throw HRESULT_FROM_WIN32(GetLastError());file.reset();
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw HRESULT_FROM_WIN32(GetLastError());
    }catch(...){DeleteFileW(temporary.c_str());throw;}
}
inline Scene LoadScene(const std::filesystem::path& path){
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!file)throw HRESULT_FROM_WIN32(GetLastError());LARGE_INTEGER size;
    if(!GetFileSizeEx(file.value,&size))throw HRESULT_FROM_WIN32(GetLastError());
    if(size.QuadPart<0||size.QuadPart>MaxDeckBytes)throw std::runtime_error("This deck is too large to open.");
    std::string bytes(static_cast<size_t>(size.QuadPart),'\0');DWORD read=0;
    if(!ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)||read!=bytes.size())throw HRESULT_FROM_WIN32(ERROR_READ_FAULT);
    return DecodeScene(bytes);
}
}
