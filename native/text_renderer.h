#pragma once
#include "editor.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

namespace callayers {
struct LumaPixel{uint32_t offset;BYTE alpha,y;};
struct ChromaPixel{uint32_t offset;BYTE alpha,u,v;};
struct TextOverlay {
    std::vector<LumaPixel> luma;
    std::vector<ChromaPixel> chroma;
};
inline std::shared_ptr<const TextOverlay> OverlayFromBgra(const BYTE* pixels,UINT stride){
    auto result=std::make_shared<TextOverlay>();
        for(UINT y=0;y<Height;y+=2)for(UINT x=0;x<Width;x+=2){
            int alpha=0,uSum=0,vSum=0;
            for(UINT dy=0;dy<2;++dy)for(UINT dx=0;dx<2;++dx){
                UINT offset=(y+dy)*Width+x+dx;const BYTE* p=pixels+(y+dy)*stride+(x+dx)*4;int a=p[3];if(!a)continue;
                int r=std::min(255,p[2]*255/a),g=std::min(255,p[1]*255/a),b=std::min(255,p[0]*255/a);
                BYTE luma=Clamp(((47*r+157*g+16*b+128)>>8)+16);
                int u=Clamp(((-26*r-87*g+112*b+128)>>8)+128),v=Clamp(((112*r-102*g-10*b+128)>>8)+128);
                result->luma.push_back({offset,static_cast<BYTE>(a),luma});alpha+=a;uSum+=u*a;vSum+=v*a;
            }
            if(alpha)result->chroma.push_back({Width*Height+(y/2)*Width+x,static_cast<BYTE>((alpha+2)/4),static_cast<BYTE>(uSum/alpha),static_cast<BYTE>(vSum/alpha)});
        }
    return result;
}
struct InkMark {InkStroke stroke;float opacity=1;};
class TextRenderer {
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> write;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IDWriteTextFormat> Format(const TextElement& t){
        ComPtr<IDWriteTextFormat> format;
        Check(write->CreateTextFormat(t.family.c_str(),nullptr,t.bold?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,static_cast<float>(t.size),L"en-us",&format));
        Check(format->SetTextAlignment(t.alignment==1?DWRITE_TEXT_ALIGNMENT_CENTER:t.alignment==2?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING));
        Check(format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));return format;
    }
public:
    TextRenderer(){
        Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.GetAddressOf()));
        Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.GetAddressOf())));
        Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    }
    void FitHeight(TextElement& t){
        auto format=Format(t);ComPtr<IDWriteTextLayout> layout;
        Check(write->CreateTextLayout(t.text.c_str(),static_cast<UINT32>(t.text.size()),format.Get(),
            static_cast<float>(t.width*Width),1000000.f,&layout));
        DWRITE_TEXT_METRICS metrics{};Check(layout->GetMetrics(&metrics));
        t.height=std::clamp<double>(std::ceil(metrics.height+2)/Height,.01,1.);
        t.y=std::min(t.y,1.-t.height);
    }
    std::shared_ptr<const TextOverlay> Render(const Scene& scene,const std::vector<InkMark>& transient={}){
        auto result=std::make_shared<TextOverlay>();if(scene.texts.empty()&&scene.images.empty()&&scene.ink.empty()&&transient.empty())return result;
        ComPtr<IWICBitmap> bitmap;Check(wic->CreateBitmap(Width,Height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
        ComPtr<ID2D1RenderTarget> target;
        auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        Check(d2d->CreateWicBitmapRenderTarget(bitmap.Get(),properties,&target));
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        target->BeginDraw();target->Clear(D2D1::ColorF(0,0.f));
        struct Layer {int order;const TextElement* text;const ImageElement* image;};std::vector<Layer> layers;
        for(const auto& image:scene.images)layers.push_back({image.order,nullptr,&image});for(const auto& text:scene.texts)layers.push_back({text.order,&text,nullptr});
        std::stable_sort(layers.begin(),layers.end(),[](const auto& a,const auto& b){return a.order<b.order;});
        for(const auto& layer:layers){
            if(layer.image){auto& image=*layer.image;if(!image.asset)continue;auto& asset=*image.asset;ComPtr<ID2D1Bitmap> bitmapImage;
                Check(target->CreateBitmap(D2D1::SizeU(asset.width,asset.height),asset.pixels.data(),asset.width*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&bitmapImage));
                target->DrawBitmap(bitmapImage.Get(),D2D1::RectF(static_cast<float>(image.x*Width),static_cast<float>(image.y*Height),static_cast<float>((image.x+image.width)*Width),static_cast<float>((image.y+image.height)*Height)),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);continue;
            }
            const auto& t=*layer.text;
            auto format=Format(t);
            ComPtr<ID2D1SolidColorBrush> brush;Check(target->CreateSolidColorBrush(D2D1::ColorF(t.color),&brush));
            auto rect=D2D1::RectF(static_cast<float>(t.x*Width),static_cast<float>(t.y*Height),static_cast<float>((t.x+t.width)*Width),static_cast<float>((t.y+t.height)*Height));
            target->DrawText(t.text.c_str(),static_cast<UINT32>(t.text.size()),format.Get(),rect,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        ComPtr<ID2D1StrokeStyle> round;
        auto style=D2D1::StrokeStyleProperties();style.startCap=style.endCap=style.dashCap=D2D1_CAP_STYLE_ROUND;style.lineJoin=D2D1_LINE_JOIN_ROUND;
        Check(d2d->CreateStrokeStyle(style,nullptr,0,&round));
        auto draw=[&](const InkStroke& stroke,float opacity){
            if(stroke.points.empty())return;ComPtr<ID2D1SolidColorBrush> brush;Check(target->CreateSolidColorBrush(D2D1::ColorF(stroke.color,opacity),&brush));
            auto point=[](InkPoint p){return D2D1::Point2F(static_cast<float>(p.x*Width),static_cast<float>(p.y*Height));};
            auto width=static_cast<float>(stroke.width);
            if(stroke.points.size()==1)target->FillEllipse(D2D1::Ellipse(point(stroke.points[0]),width/2,width/2),brush.Get());
            else for(size_t i=1;i<stroke.points.size();++i)target->DrawLine(point(stroke.points[i-1]),point(stroke.points[i]),brush.Get(),width,round.Get());
        };
        for(const auto& stroke:scene.ink)draw(stroke,1);
        for(const auto& mark:transient)draw(mark.stroke,mark.opacity);
        Check(target->EndDraw());
        std::vector<BYTE> pixels(Width*Height*4);Check(bitmap->CopyPixels(nullptr,Width*4,static_cast<UINT>(pixels.size()),pixels.data()));
        return OverlayFromBgra(pixels.data(),Width*4);
    }
};
inline std::shared_ptr<Frame> ComposeText(const Frame& background,const TextOverlay& overlay){
    auto result=std::make_shared<Frame>(background);
    for(const auto& p:overlay.luma){auto& value=result->pixels[p.offset];value=static_cast<BYTE>((value*(255-p.alpha)+p.y*p.alpha+127)/255);}
    for(const auto& p:overlay.chroma){
        auto* uv=result->pixels.data()+p.offset;
        uv[0]=static_cast<BYTE>((uv[0]*(255-p.alpha)+p.u*p.alpha+127)/255);
        uv[1]=static_cast<BYTE>((uv[1]*(255-p.alpha)+p.v*p.alpha+127)/255);
    }
    return result;
}
}
