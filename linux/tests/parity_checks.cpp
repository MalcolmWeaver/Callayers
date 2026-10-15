#include "window.h"
#include "app_style.h"
#include "card_revision.h"
#include "annotation_geometry.h"
#include "text_editor_widget.h"
#include "prompt_editor.h"
#include <QClipboard>
#include <QApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QPushButton>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFile>
#include <iostream>
#include <cstdlib>
void Check(bool value,const char* message) { if(!value){std::cerr<<message<<'\n';std::exit(1);} }
void Pump(int ms=120) { QElapsedTimer t;t.start();while(t.elapsed()<ms){QApplication::processEvents();QThread::msleep(5);} }
void Mouse(Preview* p,QEvent::Type type,QPointF logical) {
    auto rect=p->localRect({logical,QSizeF(1,1)});auto pos=rect.topLeft();
    QMouseEvent event(type,pos,QPointF(p->mapToGlobal(pos.toPoint())),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(p,&event);Pump(40);
}
void Drag(Preview* p,QPointF a,QPointF b) {Mouse(p,QEvent::MouseButtonPress,a);Mouse(p,QEvent::MouseMove,b);Mouse(p,QEvent::MouseButtonRelease,b);Pump();}
void Key(QWidget* widget,int code,QString text={},Qt::KeyboardModifiers mods=Qt::NoModifier) {
    QKeyEvent down(QEvent::KeyPress,code,mods,text);QApplication::sendEvent(widget,&down);
    QKeyEvent up(QEvent::KeyRelease,code,mods,text);QApplication::sendEvent(widget,&up);Pump(40);
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);ApplyAppStyle(app);
    QTemporaryDir temp;Check(temp.isValid(),"Temporary workspace failed");
    qputenv("XDG_DATA_HOME",temp.path().toUtf8()+"/data");qputenv("XDG_CONFIG_HOME",temp.path().toUtf8()+"/config");
    Check(app.arguments().contains("--offline-preview"),"Checks require preview-only mode");
    Card card;card.id="target";card.name="Original";card.html="<h1>Original</h1>";card.state={{"score",7}};
    Store().setCards({card});
    {
        MainWindow window;window.show();Pump(700);
        auto prompt=dynamic_cast<PromptEditor*>(window.findChild<QPlainTextEdit*>("cardPrompt"));
        Check(prompt,"Prompt editor missing");
        QApplication::clipboard()->setText(QString(1200,'a'));prompt->paste();
        Check(prompt->attachments.size()==1&&prompt->toPlainText().isEmpty(),"Long paste was not attached");
        QApplication::clipboard()->setText(QString(100000,'b'));prompt->paste();
        Check(prompt->attachments.size()==1&&prompt->toPlainText().isEmpty(),"Oversized paste changed the draft");
        prompt->attachments={};prompt->attachmentsChanged();
        auto preview=window.findChild<Preview*>();Check(preview&&preview->hasFrame(),"Preview-only workspace has no frame");
        auto annotate=window.findChild<QPushButton*>("toolPen")->parentWidget();annotate->show();Pump();
        window.findChild<QPushButton*>("toolLine")->click();Drag(preview,{700,100},{1000,240});
        auto marks=Store().annotations();Check(marks.size()==1,"Drawing did not persist");
        Check(AnnotationHit(marks[0],{850,170}),"Line hit-test misses its middle");
        window.findChild<QPushButton*>("toolSelect")->click();Drag(preview,{850,170},{800,220});
        marks=Store().annotations();Check(std::abs(marks[0].box.x()-650)<3,"Annotation selection did not move stroke");
        window.findChild<QPushButton*>("toolText")->click();Mouse(preview,QEvent::MouseButtonPress,{700,350});Mouse(preview,QEvent::MouseButtonRelease,{700,350});
        auto editor=window.findChild<TextEditorWidget*>();Pump(500);Check(editor&&editor->isVisible()&&editor->ready(),"Text editor failed to start");
        Key(editor,Qt::Key_A,"A");Key(editor,Qt::Key_Return,"\r");Key(editor,Qt::Key_B,"B");
        Check(editor->toPlainText()=="A\nB","Multiline text input failed");
        Check(Store().annotations().size()==1,"Draft text leaked into saved annotations");
        Key(editor,Qt::Key_Return,"\r",Qt::ControlModifier);Pump();
        marks=Store().annotations();Check(marks.size()==2&&marks[1].text=="A\nB","Text commit failed");
        Check(marks[1].box.height()>marks[1].font*1.8,"Multiline height did not grow");
        window.findChild<QPushButton*>("toolSelect")->click();
        Mouse(preview,QEvent::MouseButtonDblClick,{710,360});Pump(200);
        Key(editor,Qt::Key_C,"C");Key(editor,Qt::Key_Escape);Pump();
        Check(Store().annotations()[1].text=="A\nB","Escape changed completed text");
        window.findChild<QPushButton*>("toolEraser")->click();Mouse(preview,QEvent::MouseButtonPress,{800,220});Mouse(preview,QEvent::MouseButtonRelease,{800,220});
        Check(Store().annotations().size()==1,"Eraser did not delete the selected stroke");
        auto saved=Store().cards().first();
        const auto initialWidth=saved.w;
        window.findChild<QPushButton*>("toolSelect")->click();Drag(preview,{saved.x+saved.w-2,saved.y+saved.h-2},{saved.x+saved.w+98,saved.y+saved.h+50});
        saved=Store().cards().first();Check(saved.w>initialWidth+80,"Card resize failed");
        Check(std::abs(saved.w/saved.h-card.w/card.h)<0.01,"Card resize changed aspect ratio");
        if(app.arguments().contains("--screenshot"))Check(window.grab().save("/tmp/callayers-parity-workspace.png"),"Screenshot failed");
    }
    auto restored=Store();Check(restored.annotations().size()==1&&restored.annotations()[0].text=="A\nB","Restart lost annotations");
    QVector<Card> cards{card};
    QJsonObject revision{{"name","Generated name"},{"html","<h1>Updated</h1>"},{"width",480},{"height",270}};
    Check(!ApplyCardRevision(cards,"missing",0,revision),"Deleted card accepted late revision");
    Check(!ApplyCardRevision(cards,"target",1,revision),"Changed card accepted stale revision");
    Check(ApplyCardRevision(cards,"target",0,revision)&&cards[0].hasPrevious&&cards[0].previousHtml==card.html,"Revision backup failed");
    Check(cards[0].name==card.name&&cards[0].state.isEmpty(),"Revision did not preserve name or reset state");
    card.imagePath="/tmp/local.png";cards={card};
    Check(!ApplyCardRevision(cards,"target",0,revision),"Image revision dropped the local pixels");
    Store().setCards(cards={restored.cards().first()});
    auto path=temp.path()+"/data/Callayers/workspace.json";
    QFile broken(path);Check(broken.open(QIODevice::WriteOnly),"Fixture creation failed");broken.write("broken");broken.close();
    Store corrupt;Check(!corrupt.problem().isEmpty(),"Corrupt workspace was silently accepted");
    corrupt.setCards({card});Check(broken.open(QIODevice::ReadOnly)&&broken.readAll()=="broken","Corrupt original was overwritten");
    std::cout<<"Preview-only annotation, HTML text, resize, persistence, and revision checks passed\n";
}
