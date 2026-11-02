"""Compile the current menu method against Qt; synthetic INI values only.
Plugin dispatch and native dialogs are intentionally outside this focused check.
Usage: python3 this_file --source src/gui/documentviewer/WizTitleBar.cpp --sdk /path/to/Qt --output /new/test/root
"""
from pathlib import Path
import argparse, subprocess, json
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
s=a.source.read_text(encoding='utf-8-sig');start=s.index('QMenu* WizTitleBar::createEditorMenu()');end=s.index('\nvoid WizTitleBar::handlePluginEditorActionTriggered()',start);method=s[start:end]
pre=r'''
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QMenu>
#include <QAction>
#include <QMap>
#include <QVariant>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>
#include <cstdio>
static QString store; namespace Utils { namespace WizPathResolve { QString dataStorePath(){return store+"/";} } }
static int made=0,gone=0;class TrackedSettings:public QSettings{public:using QSettings::QSettings;TrackedSettings(const QString& f,Format fmt):QSettings(f,fmt){made++;}~TrackedSettings(){gone++;}};
class JSPluginModule{};class JSPluginManager {public:static JSPluginManager& instance(){static JSPluginManager m;return m;}QList<JSPluginModule*>modulesByKeyValue(const QString&,const QString&){return {};}QAction*createPluginAction(QObject* p,JSPluginModule*){return new QAction(p);}};
enum Mode{modeReader,modeEditor};class Note{public:Mode mode=modeReader;Mode editorMode(){return mode;}};
class WizTitleBar:public QWidget{public:Note note;Note*noteView(){return &note;}QMenu*createEditorMenu();void handleDiscardChanges(){}void handlePluginEditorActionTriggered(){}};
#define QSettings TrackedSettings
'''
post=r'''
#undef QSettings
int main(int argc,char**argv){QCoreApplication::setAttribute(Qt::AA_PluginApplication);QApplication app(argc,argv);QTemporaryDir dir(QString::fromLocal8Bit(argv[1])+"/settings-XXXXXX");if(!dir.isValid())return 3;store=dir.path();int checks=0;bool pass=true;
auto check=[&](bool value){checks++;pass&=value;};
{QSettings ini(store+"/externalEditor.ini",QSettings::IniFormat);ini.beginGroup("first");ini.setValue("Name","Synthetic editor");ini.setValue("ProgramFile","/synthetic/nonexistent");ini.setValue("Arguments","--new %1");ini.setValue("TextEditor",1);ini.setValue("UTF8Encoding",1);ini.setValue("OpenShortCut","Ctrl+Alt+E");ini.endGroup();ini.beginGroup("second");ini.setValue("Name","Defaults");ini.setValue("ProgramFile","/synthetic/other");ini.endGroup();ini.sync();}
WizTitleBar bar;
for(int i=0;i<100;i++){std::unique_ptr<QMenu> menu(bar.createEditorMenu());check(made==gone);int found=0;for(auto* ac:menu->actions()){const auto m=ac->data().toMap();if(m.isEmpty())continue;found++;if(m.value("Name")=="Synthetic editor"){check(m.value("ProgramFile")=="/synthetic/nonexistent");check(m.value("Arguments")=="--new %1");check(m.value("TextEditor").toInt()==1&&m.value("UTF8Encoding").toInt()==1);check(ac->shortcut()==QKeySequence::fromString("Ctrl+Alt+E"));}else {check(m.value("Name")=="Defaults");check(m.value("Arguments")=="%1");check(m.value("TextEditor").toInt()==0&&m.value("UTF8Encoding").toInt()==0&&ac->shortcut().isEmpty());}}check(found==2);}
bar.note.mode=modeEditor;{const int before=made;std::unique_ptr<QMenu>menu(bar.createEditorMenu());check(menu->actions().size()==1&&menu->actions()[0]->text()=="Discard changes");check(made==before);}bar.note.mode=modeReader;
{QSettings ini(store+"/externalEditor.ini",QSettings::IniFormat);ini.clear();ini.setValue("updated/Name","Updated");ini.setValue("updated/ProgramFile","/synthetic/new");ini.sync();}
{std::unique_ptr<QMenu> menu(bar.createEditorMenu());int found=0;for(auto*ac:menu->actions())if(!ac->data().toMap().isEmpty()){found++;check(ac->data().toMap().value("Name")=="Updated");}check(found==1&&made==gone);}
{QSettings ini(store+"/externalEditor.ini",QSettings::IniFormat);ini.clear();ini.sync();}
{std::unique_ptr<QMenu> menu(bar.createEditorMenu());check(menu->actions().size()==2);check(made==gone);}
const auto result=QJsonDocument(QJsonObject{{"passed",pass},{"checks",checks},{"created",made},{"destroyed",gone},{"actualMethodExtracted",true},{"pluginActionsExercised",false}}).toJson();fwrite(result.constData(),1,result.size(),stdout);return pass?0:1;}
'''
cpp=a.output/'main.cpp';cpp.write_text(pre+method+post)
cmd=['clang++','-std=c++17','-fPIC',str(cpp),'-o',str(a.output/'test'),'-F'+str(a.sdk/'lib'),'-Wl,-rpath,'+str(a.sdk/'lib')]
for n in ['QtWidgets','QtGui','QtCore']:cmd+=['-I'+str(a.sdk/'lib'/f'{n}.framework/Headers'),'-framework',n]
r=subprocess.run(cmd,capture_output=True,text=True);(a.output/'compile.log').write_text(r.stdout+r.stderr)
if r.returncode:raise SystemExit(r.returncode)
r=subprocess.run([str(a.output/'test'),str(a.output),'-platform','cocoa'],capture_output=True,text=True);(a.output/'result.json').write_text(r.stdout);(a.output/'runtime.log').write_text(r.stderr);print(r.stdout);raise SystemExit(r.returncode)
