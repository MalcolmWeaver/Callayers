#include "scene_renderer.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QKeyEvent>
#include <QJsonDocument>
#include <iostream>
#include <cstdlib>
void Check(bool ok, const char* message) { if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
bool Paint(SceneRenderer& renderer, QVector<Card>& cards) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 5000) {
        if (renderer.render(cards, {})) return true;
        if (!renderer.problem().isEmpty()) { std::cerr << renderer.problem().toStdString() << '\n'; return false; }
        QApplication::processEvents(); QThread::msleep(10);
    }
    return false;
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    SceneRenderer renderer;
    Card card; card.id="fixture"; card.name="Counter"; card.x=0; card.y=0;
    card.html=R"(<style>body{background:#123456}button{width:100px;height:80px;background:#ff0000}</style><button id='add'>0</button><input id='name'><script>
let count=0;add.onclick=()=>{add.textContent=String(++count)};
window.CallayersCard={snapshot:()=>({count}),restore:s=>{count=s.count||0;add.textContent=String(count)}};
</script>)";
    QVector<Card> cards{card};
    Check(Paint(renderer,cards),"Renderer failed to produce an initial frame");
    Check(renderer.image().size()==QSize(1920,1080),"Unexpected render size");
    renderer.mouse(1,{40,40}); renderer.mouse(2,{40,40});
    Check(Paint(renderer,cards),"Click failed to paint");
    Check(renderer.checkpoint(cards) && cards[0].state["count"].toInt()==1,"Real pointer input or snapshot failed");
    cards[0].w=960; cards[0].h=540;
    Check(Paint(renderer,cards),"Resize failed");
    Check(renderer.evaluate("add.textContent","card-0")=="1","Resize reloaded live card");
    renderer.mouse(1,{80,80}); renderer.mouse(2,{80,80}); Paint(renderer,cards); renderer.checkpoint(cards);
    Check(cards[0].state["count"].toInt()==2,"Scaled pointer mapping failed");
    cards[0].visible=false; Check(Paint(renderer,cards),"Hide failed");
    Check(renderer.evaluate("document.querySelectorAll('iframe').length")=="0","Hidden document was not unloaded");
    cards[0].visible=true; Check(Paint(renderer,cards),"Show failed");
    Check(renderer.evaluate("add.textContent","card-0")=="2","Saved state failed to restore");
    Check(renderer.evaluate("document.querySelector('iframe').getAttribute('sandbox')")=="allow-scripts","Card sandbox missing");
    Check(renderer.evaluate("document.querySelector('meta[http-equiv]').content","card-0").contains("connect-src 'none'"),"Network policy missing");
    auto input=QJsonDocument::fromJson(renderer.evaluate("JSON.stringify((()=>{let r=document.getElementById('name').getBoundingClientRect();return {x:r.x+5,y:r.y+5}})())","card-0").toUtf8()).object();
    const QPointF focusPoint(input["x"].toDouble()*2,input["y"].toDouble()*2);
    renderer.mouse(1,focusPoint);renderer.mouse(2,focusPoint);Paint(renderer,cards);
    QKeyEvent key(QEvent::KeyPress,Qt::Key_A,Qt::NoModifier,"a");renderer.key(&key);
    Paint(renderer,cards);
    Check(renderer.evaluate("document.getElementById('name').value","card-0")=="a","HTML keyboard input failed");
    renderer.suspend(cards); Check(Paint(renderer,cards),"Resume failed");
    Check(renderer.evaluate("add.textContent","card-0")=="2","Suspend discarded state");
    Annotation text;text.id=123;text.kind=Tool::Text;text.text="Completed text";text.box={20,600,500,90};text.font=50;
    QElapsedTimer paint;paint.start();
    bool privateReady=false;
    while(paint.elapsed()<5000) {
        if(renderer.render(cards,{text},123) && renderer.previewImage()!=renderer.image()) {privateReady=true;break;}
        QApplication::processEvents();QThread::msleep(10);
    }
    Check(privateReady,"Private preview did not hide completed text");
    const auto completed=renderer.image();
    Check(renderer.render(cards,{text},0),"Text restore failed");
    Check(renderer.image()==completed,"Editing changed the completed scene");
    std::cout << "HTML painting, scaled input, state, hide/show, suspend and sandbox checks passed\n";
}
