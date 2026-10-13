#include "text_editor_ui.h"
#include <shlobj.h>
#include <iostream>
using namespace callayers;
namespace {
void Expect(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
TextEditorUI* ui=nullptr;
LRESULT CALLBACK Proc(HWND window,UINT msg,WPARAM wp,LPARAM lp){if(ui){LRESULT result;if(ui->Message(msg,wp,lp,result))return result;}return DefWindowProcW(window,msg,wp,lp);}
std::vector<BYTE> Fixture(){
    ComPtr<IWICImagingFactory> wic;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    constexpr UINT w=200,h=120;std::vector<BYTE> pixels(w*h*4);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto p=pixels.data()+(y*w+x)*4;if(x<w/2&&y<h/2)continue;p[3]=255;if(y<h/2)p[2]=255;else if(x<w/2)p[1]=255;else p[0]=p[1]=p[2]=255;}
    ComPtr<IWICBitmap> bitmap;Check(wic->CreateBitmapFromMemory(w,h,GUID_WICPixelFormat32bppBGRA,w*4,static_cast<UINT>(pixels.size()),pixels.data(),&bitmap));
    ComPtr<IStream> stream;Check(CreateStreamOnHGlobal(nullptr,TRUE,&stream));ComPtr<IWICBitmapEncoder> encoder;Check(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;Check(encoder->CreateNewFrame(&frame,nullptr));Check(frame->Initialize(nullptr));Check(frame->SetSize(w,h));auto format=GUID_WICPixelFormat32bppBGRA;Check(frame->SetPixelFormat(&format));Check(frame->WriteSource(bitmap.Get(),nullptr));Check(frame->Commit());Check(encoder->Commit());
    STATSTG stat{};Check(stream->Stat(&stat,STATFLAG_NONAME));HGLOBAL memory;Check(GetHGlobalFromStream(stream.Get(),&memory));auto p=static_cast<BYTE*>(GlobalLock(memory));std::vector<BYTE> bytes(p,p+stat.cbSize.LowPart);GlobalUnlock(memory);return bytes;
}
void WriteBytes(const std::filesystem::path& path,const std::vector<BYTE>& bytes){Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));Check(file?S_OK:E_FAIL);DWORD written=0;Check(WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()?S_OK:E_FAIL);}
}
int wmain(int argc,wchar_t** argv){
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    try{
        auto bytes=Fixture();if(argc==3&&std::wstring_view(argv[1])==L"--fixture"){WriteBytes(argv[2],bytes);return 0;}
        ImageCodec codec;auto asset=codec.Import(bytes);Expect(asset->width==200&&asset->height==120,"image dimensions changed");Expect(asset->pixels[3]==0,"PNG transparency lost");
        auto path=std::filesystem::current_path()/(L"image-test-"+NewId()+L".png");WriteBytes(path,bytes);ImageElement image;image.id=NewId();image.asset=codec.LoadFile(path);image.x=.25;image.y=.25;image.width=.5;image.height=.5;image.order=2;
        Scene scene;scene.images={image};auto saved=path;saved+=L".callayers.json";SaveScene(saved,scene);std::filesystem::remove(path);
        Expect(LoadScene(saved)==scene,"embedded image depends on original file or lost placement");std::filesystem::remove(saved);
        auto moved=MoveImage(image,2,2);Expect(moved.x+moved.width<=1&&moved.y+moved.height<=1,"image movement escaped stage");
        for(int handle=0;handle<6;++handle){auto resized=ResizeImage(image,handle,.2,.1);Expect(std::abs(resized.width/resized.height-image.width/image.height)<1e-9,"image resized with distortion");}
        TextRenderer renderer;auto background=Pattern(0,true);auto composed=ComposeText(*background,*renderer.Render(scene));
        Expect(composed->pixels[350*Width+600]==background->pixels[350*Width+600],"transparent image pixels covered webcam");
        Expect(composed->pixels[350*Width+1200]!=background->pixels[350*Width+1200],"image pixels missing from composition");
        TextElement text;text.id=NewId();text.text=L"Visible";text.color=0x000000;text.x=.55;text.y=.6;text.width=.15;text.height=.1;text.order=1;scene.texts={text};
        Expect(ComposeText(*background,*renderer.Render(scene))->pixels==composed->pixels,"text behind an image leaked through");scene.texts[0].order=3;Expect(ComposeText(*background,*renderer.Render(scene))->pixels!=composed->pixels,"text above an image missing");
        bool rejected=false;try{codec.LoadEmbedded(L"not a PNG");}catch(...){rejected=true;}Expect(rejected,"invalid embedded image accepted");
        BITMAPINFOHEADER dib{};dib.biSize=sizeof(dib);dib.biWidth=2;dib.biHeight=2;dib.biPlanes=1;dib.biBitCount=24;std::vector<BYTE> raw(sizeof(dib)+16,255);memcpy(raw.data(),&dib,sizeof(dib));
        auto pasted=codec.FromDib(raw.data(),raw.size());Expect(pasted->width==2&&pasted->height==2&&pasted->pixels[3]==255,"clipboard DIB conversion failed");
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Proc;wc.lpszClassName=L"CallayersImageTest";RegisterClassW(&wc);HWND window=CreateWindowW(wc.lpszClassName,L"Image tests",WS_OVERLAPPEDWINDOW,0,0,1100,800,nullptr,nullptr,wc.hInstance,nullptr);
        std::shared_ptr<const TextOverlay> output;auto editor=std::make_unique<TextEditorUI>(window,[&](auto layer){output=layer;},false);ui=editor.get();ui->SetStage({20,165,980,705});
        Scene shown;ui->SceneChanged=[&](const Scene& next){shown=next;};
        ui->AddImages({image});Expect(ui->CardSelected()&&ui->CurrentScene().cards.size()==1&&ui->CurrentScene().images.empty()&&shown.cards.empty(),"image import must create an unchecked card");
        auto original=ui->CurrentScene();auto current=original.cards[0];
        Expect(current.assets.size()==1&&current.assets[0].dataUri->starts_with(L"data:image/png;base64,"),"image card lost embedded pixels");
        Expect(DecodeScene(EncodeScene(original))==original,"image card did not survive save/load");
        ui->SetCardShown(0,true);Expect(shown.cards.size()==1&&shown.cards[0].assets==current.assets,"image card did not publish when checked");
        ui->SelectCard(0);ui->Command(TextEditorUI::DeleteId);Expect(ui->CurrentScene().cards.empty()&&shown.cards.empty(),"image card delete failed");ui->Command(TextEditorUI::UndoId);Expect(ui->CurrentScene()==original&&shown.cards.empty(),"image card delete undo failed");
        WriteBytes(path,bytes);std::wstring dropPath=path.wstring();size_t dropSize=sizeof(DROPFILES)+(dropPath.size()+2)*sizeof(wchar_t);HGLOBAL memory=GlobalAlloc(GHND,dropSize);auto drop=static_cast<DROPFILES*>(GlobalLock(memory));drop->pFiles=sizeof(DROPFILES);drop->fWide=TRUE;drop->pt={650,450};memcpy(reinterpret_cast<BYTE*>(drop)+sizeof(DROPFILES),dropPath.c_str(),(dropPath.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);
        SendMessageW(window,WM_DROPFILES,reinterpret_cast<WPARAM>(memory),0);Expect(ui->CurrentScene().cards.size()==2,"file drop did not import image card");auto droppedId=ui->CurrentScene().cards.back().id;ui->MoveSelectedCard(-1);Expect(ui->CurrentScene().cards.front().id==droppedId,"image card reordering failed");
        auto beforeFailure=ui->CurrentScene();rejected=false;try{ui->ImportImages({path,path.wstring()+L".missing"});}catch(...){rejected=true;}Expect(rejected&&ui->CurrentScene()==beforeFailure,"failed batch import partially changed scene");std::filesystem::remove(path);
        ui=nullptr;editor.reset();DestroyWindow(window);std::cout<<"PASS: image import, alpha, clipboard bitmap conversion, embedded save/load, ordering, drag/drop, delete/undo, atomic failure\n";return 0;
    }catch(const winrt::hresult_error& e){std::wcerr<<e.message().c_str()<<L"\n";}catch(HRESULT hr){std::wcerr<<ErrorText(hr)<<L"\n";}catch(const std::exception& e){std::cerr<<e.what()<<"\n";}return 1;
}
