#include <contentCase/ContentCase.h>
#include <contentCase/ContentCaseDocument.h>
#include <tsre/fileFunctions/TS.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>
#include <QTextStream>
#include <QtEndian>
#include <stdexcept>

namespace {
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
void put(const QString &path,const QByteArray &bytes) {
    check(QDir().mkpath(QFileInfo(path).absolutePath()),"fixture directory");QFile f(path);
    check(f.open(QIODevice::WriteOnly|QIODevice::NewOnly),"fixture new file");check(f.write(bytes)==bytes.size(),"fixture write");
}
QByteArray word(quint32 n){QByteArray b(4,Qt::Uninitialized);qToLittleEndian(n,b.data());return b;}
QByteArray wide(const QString &s){QByteArray b;for(auto c:s){char v[2];qToLittleEndian<quint16>(c.unicode(),v);b.append(v,2);}return b;}
QByteArray string(const QString &s){QByteArray b(2,Qt::Uninitialized);qToLittleEndian<quint16>(s.size(),b.data());return b+wide(s);}
QByteArray block(TS::TokenId id,QByteArray payload){payload.prepend(char(0));return word(id)+word(payload.size())+payload;}
QByteArray binary(QByteArray payload){return QByteArray("SIMISA@@@@@@@@@@JINX0s1b________")+payload;}
QByteArray compressed(QByteArray plain){const auto body=plain.mid(16);return QByteArray("SIMISA@F")+word(body.size())+QByteArray("@@@@")+qCompress(body).mid(4);}
bool edge(const QJsonObject &plan,const QString &kind,const QString &status){for(const auto &v:plan["references"].toArray()){const auto e=v.toObject();if(e["kind"]==kind&&e["status"]==status)return true;}return false;}
QString plannedLeaf(const QJsonObject &plan,const QString &path) {
    for(const auto &v:plan["operations"].toArray()) {
        const auto op=v.toObject();if(op["operation"]=="rename-file" && op["from"]==path)return op["to"].toString().section('/',-1);
    }
    return path.section('/',-1);
}
bool errorCode(const QJsonObject &plan,const QString &code) {
    for(const auto &v:plan["failures"].toArray())if(v.toObject()["code"]==code)return true;
    return false;
}
int command(const QStringList &args){QProcess p;p.start(QCoreApplication::applicationFilePath(),QStringList{"--command"}+args);check(p.waitForFinished(30000),"command completion");return p.exitCode();}
}
int main(int argc,char **argv) {
    if(argc>1 && QByteArray(argv[1])=="--command")return ContentCase::run(argc-1,argv+1);
    QCoreApplication app(argc,argv);
    try {
        const auto s=binary(block(TS::shape,block(TS::images,word(1)+block(TS::image,string("Bark.ace")))));
        for(const auto &bytes:{s,compressed(s)}) {
            const auto d=ContentCase::inspectDocument(bytes,"s");check(d.valid&&d.fields.size()==1,"binary shape reference");
            check(d.fields[0].values[0].text=="Bark.ace","binary string preserves case");
        }
        const auto t=binary(block(TS::terrain,block(TS::terrain_samples,block(TS::terrain_sample_ybuffer,string("Tile_Y.raw")))+
            block(TS::terrain_shaders,word(1)+block(TS::terrain_shader,string("shader")+
                block(TS::terrain_texslots,word(1)+block(TS::terrain_texslot,string("Grass.ace")+word(0)+word(0)))))));
        check(ContentCase::inspectDocument(t,"t").fields.size()==2,"terrain strings without terrain/editor construction");
        const auto bakeRecord=block(TS::TSRETerrainBakedMaterial,string("Winter")+word(0)+word(0)+word(1024)+string("settingshash")+string("sourceshash")+string("validationhash"));
        const auto terrainMetadata=binary(block(TS::terrain,block(TS::terrain_samples,
            block(TS::TSRETerrainMaterialMap,word(1)+word(2)+word(42))+
            block(TS::TSRETerrainBakedMaterials,word(2)+word(0)+word(0)+bakeRecord))));
        const auto metadata=ContentCase::inspectDocument(terrainMetadata,"t");
        check(metadata.referenceScanComplete && metadata.fields.isEmpty(),"terrain UID and bake metadata are not filename references");
        check(!ContentCase::inspectDocument(terrainMetadata.left(terrainMetadata.size()-1),"t").referenceScanComplete,"truncated bake metadata retains an error");
        const auto w=binary(block(TS::Tr_Worldfile,block(TS::Static,block(TS::FileName,string("Tree.s")))));
        check(ContentCase::inspectDocument(w,"w").fields.size()==1,"world FileName field");
        check(!ContentCase::inspectDocument(s.left(s.size()-1),"s").valid,"truncated binary rejected");
        check(!ContentCase::inspectDocument(binary(block(TS::image,string("Bark.ace"))),"s").valid,"invalid binary image root rejected safely");
        check(!ContentCase::inspectDocument(binary(block(TS::shape,block(TS::images,word(0xffffffff)))),"s").valid,"negative binary count rejected");
        auto malformed=compressed(s);malformed[8]=char(1);check(!ContentCase::inspectDocument(malformed,"s").valid,"bad compressed length rejected");
        const auto text=QByteArray("SIMISA@@@@@@@@@@JINX0s1t______\nshape ( comment ( image ( Fake.ace ) ) images ( 1 image ( \"Bark\" + \".ace\" ) ) )");
        auto d=ContentCase::inspectDocument(text,"s");check(d.valid&&d.fields.size()==1&&d.fields[0].values[0].text=="Bark.ace","text composition and comments");
        check(d.fields[0].name=="image"&&d.fields[0].parents==QStringList({"shape","images"}),"retained field names own lexer text");
        const auto catalog=ContentCase::inspectDocument("SIMISA@@@@@@@@@@JINX0v1t______\n1\nCarSpawnerItem ( Car.s 5 )\n)","catalog");
        check(catalog.valid&&catalog.fields.size()==1&&catalog.diagnostics.isEmpty(),"native counted catalog trailer");
        const auto trailing=ContentCase::inspectDocument("world ( terrain_texslot ( Sky.ace ) ) )", "env");
        check(!trailing.valid && trailing.referenceScanComplete && trailing.fields.size()==1,"trailing syntax warning preserves filename discovery");
        const auto annotation=ContentCase::inspectDocument("Wagon ( WagonShape ( Body.s ) (#_fire temp, mass ) Sound ( Gen.sms ) )","eng");
        check(!annotation.valid && annotation.referenceScanComplete && annotation.fields.size()==2,"legacy annotation recovery continues to later references");
        const auto missingOuter=ContentCase::inspectDocument("CarSpawnerList ( CarSpawnerItem ( Car.s 1 )","catalog");
        check(!missingOuter.valid && missingOuter.referenceScanComplete && missingOuter.fields.size()==1,"missing outer delimiter retains complete filename fields");
        const auto missingField=ContentCase::inspectDocument("Wagon ( WagonShape ( Body.s","wag");
        check(!missingField.referenceScanComplete,"unfinished filename field is a discovery failure");
        const auto quoteError=ContentCase::inspectDocument("Wagon ( comment ( \"unfinished ) WagonShape ( Body.s ) )","wag");
        check(!quoteError.referenceScanComplete && quoteError.diagnostics.join(' ').contains("Unterminated quoted string"),"skipped block failure keeps the actual lexer error");
        const auto commentQuote=ContentCase::inspectDocument("Wagon ( comment ( 106.747t full\" ) Sound ( Wagon.sms ) )","wag");
        check(commentQuote.referenceScanComplete && commentQuote.fields.size()==1 && commentQuote.fields[0].name=="sound","quote inside comment atom cannot swallow later filename fields");
        const auto geometryError=ContentCase::inspectDocument("shape ( images ( 1 image ( Bark.ace ) ) points ( 5 point ( 1 2","s");
        check(!geometryError.valid && geometryError.referenceScanComplete && geometryError.fields.size()==1,"incomplete geometry does not discard the complete image table");
        check(!ContentCase::inspectDocument("shape ( points ( 1 point ( 0 0","s").referenceScanComplete,"unread image table remains a filename discovery failure");
        check(!ContentCase::inspectDocument("world ( ) ) world ( terrain_texslot ( X.ace ) )","env").referenceScanComplete,"unexpected delimiter before more data does not certify context");
        const auto unicode=QByteArray("\xff\xfe",2)+wide(QString::fromUtf8(text));
        check(ContentCase::inspectDocument(unicode,"s").valid,"UTF16 text");
        const auto body=unicode.mid(34);
        const auto zipWide=QByteArray("\xff\xfe",2)+wide("SIMISA@F")+word(body.size())+wide("@@@@@@")+qCompress(body).mid(4);
        check(ContentCase::inspectDocument(zipWide,"s").valid,"compressed Unicode envelope");
        QTemporaryDir tmp;check(tmp.isValid(),"temporary directory");const QString root=tmp.path()+"/GameRoot";
        put(root+"/routes/RouteOne/WORLD/test.w",w);
        put(root+"/routes/RouteOne/RouteOne.trk","Tr_RouteFile ( Name ( RouteOne ) )");
        put(root+"/routes/RouteOne/SHAPES/TREE.s",s);
        put(root+"/routes/RouteOne/TEXTURES/bark.ace","leaf bytes");
        put(root+"/GLOBAL/TSECTION.DAT","TrackSections ( 0 ) TrackShapes ( 0 )");
        put(root+"/GLOBAL/SHAPES/Unused.s",s);
        put(root+"/routes/RouteOne/ACTIVITIES/a.act","Tr_Activity ( Player_Service_Definition ( Express ) )");
        put(root+"/routes/RouteOne/SERVICES/Express.srv","Service_Definition ( Train_Config ( FastTrain ) PathID ( Morning ) )");
        put(root+"/routes/RouteOne/PATHS/Morning.pat","TrackPath ( TrPathName ( Morning ) )");
        put(root+"/routes/Backups/Snapshot/Snapshot.trk","Tr_RouteFile ( Name ( Snapshot ) )");
        put(root+"/routes/Backups/Snapshot/SERVICES/Backup.srv","Service_Definition ( PathID ( Local ) )");
        put(root+"/routes/Backups/Snapshot/PATHS/Local.pat","TrackPath ( TrPathName ( Local ) )");
        put(root+"/TRAINS/CONSISTS/FastTrain.con","Train ( TrainCfg ( x Engine ( EngineData ( Loco Product ) ) ) )");
        put(root+"/TRAINS/TRAINSET/Product/Loco.eng","Wagon ( Include ( Common.inc ) WagonShape ( Body.s ) )");
        put(root+"/TRAINS/TRAINSET/Product/Common.inc","Wagon ( Include ( Common.inc ) )");
        put(root+"/TRAINS/TRAINSET/Product/Body.s","shape ( images ( 0 ) )");
        QString error;const auto before=ContentCase::scan(root,error);check(error.isEmpty(),"fixture scan");
        check(edge(before,"shape","case-mismatch")&&edge(before,"texture","case-mismatch"),"case mismatch extraction");
        check(!edge(before,"texture","context-unbound"),"global images evaluated in route texture contexts");
        bool fixedName=false;for(const auto &v:before["operations"].toArray())if(v.toObject()["from"]=="GLOBAL/TSECTION.DAT")
            fixedName=v.toObject()["to"]=="GLOBAL/tsection.dat";
        check(fixedName,"fixed system basename convention planned");
        check(edge(before,"service","exact")&&edge(before,"consist","exact")&&edge(before,"enginedata","exact"),"activity-to-train graph");
        for(const auto &v:before["references"].toArray()) {
            const auto e=v.toObject();if(e["spelling"]=="Local.pat")
                check(false,"custom backup container must not supply reference edges");
        }
        bool excludedBackup=false;
        for(const auto &v:before["files"].toArray())if(v.toObject()["path"]=="routes/Backups/Snapshot/Snapshot.trk")
            excludedBackup=v.toObject()["coverage"]=="excluded-by-scope";
        check(excludedBackup,"custom route container is excluded even with nested descriptors");
        for(const auto &v:before["operations"].toArray())check(v.toObject()["from"]!="routes/Backups/Snapshot/Snapshot.trk","excluded descriptor has no rename proposal");
        check(!before["includeCycles"].toArray().isEmpty(),"include cycle reported without recursion loop");
        check(!before["applyReady"].toBool()&&!before["coverageCertified"].toBool(),"no unsafe certification");
        for(const auto &value:before["operations"].toArray()) {
            const auto op=value.toObject();
            check(op["operation"]!="edit-reference","preserved dynamic directories need no reference rewrite");
            check(op["from"]!="TRAINS/TRAINSET/Product","enumerated product names retain their spelling");
            if(op["from"]=="routes/RouteOne")check(op["to"]=="ROUTES/ROUTEONE","route directory convention is uppercase");
        }
        const auto after=ContentCase::scan(root,error);
        const auto a=QJsonDocument(before).toJson(QJsonDocument::Compact),b=QJsonDocument(after).toJson(QJsonDocument::Compact);
        if(a!=b){int pos=0;while(pos<std::min(a.size(),b.size())&&a[pos]==b[pos])++pos;
            QTextStream(stderr)<<"First difference at "<<pos<<"\n"<<a.mid(std::max(0,pos-80),250)<<"\n"<<b.mid(std::max(0,pos-80),250)<<"\n";}
        check(a==b,"repeat scan deterministic and unchanged inputs");
        check(command({"--contentcase",root})==2,"default mutation disabled");
        check(command({"--contentcase",root,"--apply","anything.json"})==2,"apply disabled");
        check(command({"--refreshpmaptextures","--contentcase",root,"--plan"})==2,"mixed command modes refused");
        check(command({"--contentcase",root,"--plan","--report",root+"/forbidden.md"})==2,"root output refused");
        check(!QFile::exists(root+"/forbidden.md"),"root output never created");
        check(command({"--contentcase",root,"--plan","--report",tmp.path()+"/GameRoot/../GameRoot/alias.md"})==2,"lexical output alias into root refused");
        put(tmp.path()+"/existing.md","keep");
        check(command({"--contentcase",root,"--plan","--report",tmp.path()+"/existing.md"})==2,"existing output refused");
        check(command({"--contentcase",root,"--plan",tmp.path()+"/plan.json","--report",tmp.path()+"/report.md"})==1,"optional outputs retain explicit unresolved-context/cycle failures");
        check(ContentCase::scan(root,error)["inspectedContentSha256"]==before["inspectedContentSha256"],"CLI leaves source bytes intact");
        const QString sharedRoot=tmp.path()+"/SharedRoot";
        put(sharedRoot+"/GLOBAL/tsection.dat","TrackShapes ( TrackShape ( 1 FileName ( Shared.s ) ) )");
        put(sharedRoot+"/GLOBAL/SHAPES/Shared.s","shape ( images ( 1 image ( BARK.ace ) ) )");
        for(const auto &route:{QString("One"),QString("Two")}) {
            const QString routePath=sharedRoot+"/ROUTES/"+route;
            const QByteArray spelling=route=="One"?"Bark.ace":"bark.ace";
            put(routePath+"/route.trk","Tr_RouteFile ( Name ( Route ) )");
            put(routePath+"/TEXTURES/"+QString::fromLatin1(spelling),"texture");
            for(const auto &shape:{"First.s","Second.s"})
                put(routePath+"/SHAPES/"+shape,"shape ( images ( 1 image ( "+spelling+" ) ) )");
        }
        const auto shared=ContentCase::scan(sharedRoot,error);
        check(shared["referenceConflicts"].toArray().isEmpty() && shared["failures"].toArray().isEmpty(),"shared image field gets one coordinated spelling across routes");
        check(plannedLeaf(shared,"ROUTES/One/TEXTURES/Bark.ace")==plannedLeaf(shared,"ROUTES/Two/TEXTURES/bark.ace"),"separate route textures get matching names");
        check(shared["textureNamingGroups"].toArray().size()==1 &&
            shared["textureNamingGroups"].toArray().first().toObject()["fileIds"].toArray().size()==2,"coordination preserves both physical texture identities");
        int sharedEdits=0;
        for(const auto &v:shared["operations"].toArray()) {
            const auto op=v.toObject();
            if(op["operation"]=="edit-reference" && op["originalLogicalReference"]=="BARK.ace") {
                ++sharedEdits;check(op["edgeIds"].toArray().size()==2,"one field edit retains both route context edges");
            }
        }
        check(sharedEdits==1,"shared field edit is proposed once");
        put(sharedRoot+"/ROUTES/One/SHAPES/Frozen.s","shape ( images ( 1 image ( BARK.ace ) ) ) )");
        auto frozenShared=ContentCase::scan(sharedRoot,error);
        check(frozenShared["failures"].toArray().isEmpty(),"one frozen spelling coordinates the whole component");
        check(plannedLeaf(frozenShared,"ROUTES/Two/TEXTURES/bark.ace")=="BARK.ace","frozen source wins over several editable source votes in another route");
        put(sharedRoot+"/ROUTES/Two/SHAPES/Opposed.s","shape ( images ( 1 image ( bark.ace ) ) ) )");
        put(sharedRoot+"/ROUTES/One/SHAPES/Independent.s","shape ( images ( 1 image ( Other.ace ) ) )");
        put(sharedRoot+"/ROUTES/One/TEXTURES/OTHER.ace","independent texture");
        frozenShared=ContentCase::scan(sharedRoot,error);
        check(errorCode(frozenShared,"incompatible-texture-naming-constraints"),"opposing frozen references across routes remain an explicit conflict");
        check(frozenShared["textureNamingGroups"].toArray().first().toObject()["decision"].toString().startsWith("blocked;"),"unsatisfiable group is marked blocked");
        bool independentRename=false;
        for(const auto &v:frozenShared["operations"].toArray()) {
            const auto op=v.toObject();
            if(op["operation"]=="edit-reference")check(op["readiness"].toString().startsWith("blocked;"),"edits in an unsatisfied texture component remain blocked");
            if(op["from"]=="ROUTES/One/TEXTURES/OTHER.ace")independentRename=!op["readiness"].toString().startsWith("blocked;");
        }
        check(independentRename,"unsatisfied naming component does not block an independent texture");

        const QString connectedRoot=tmp.path()+"/ConnectedRoot";
        for(const auto &route:{"A","B","C"}) {
            put(connectedRoot+"/ROUTES/"+route+"/route.trk","Tr_RouteFile ( Name ( Route ) )");
            put(connectedRoot+"/ROUTES/"+route+"/TEXTURES/Concrete.ace",QByteArray("distinct texture ")+route);
        }
        put(connectedRoot+"/library/AB.s","shape ( images ( 1 image ( Concrete.ace ) ) )");
        put(connectedRoot+"/library/BC.s","shape ( images ( 1 image ( CONCRETE.ace ) ) )");
        put(connectedRoot+"/ROUTES/A/WORLD/a.w","Tr_Worldfile ( Static ( FileName ( ../../../library/AB.s ) ) )");
        put(connectedRoot+"/ROUTES/B/WORLD/b.w","Tr_Worldfile ( Static ( FileName ( ../../../library/AB.s ) ) Static ( FileName ( ../../../library/BC.s ) ) )");
        put(connectedRoot+"/ROUTES/C/WORLD/c.w","Tr_Worldfile ( Static ( FileName ( ../../../library/BC.s ) ) )");
        put(connectedRoot+"/ROUTES/C/TEXTURES/SNOW/concrete.ace","distinct snow texture");
        const auto connected=ContentCase::scan(connectedRoot,error);
        check(connected["failures"].toArray().isEmpty() && connected["referenceConflicts"].toArray().isEmpty(),"overlapping shared fields coordinate transitively");
        check(connected["textureNamingGroups"].toArray().size()==1 &&
            connected["textureNamingGroups"].toArray().first().toObject()["fileIds"].toArray().size()==4,"transitive component includes three routes and an existing season");
        check(plannedLeaf(connected,"ROUTES/C/TEXTURES/SNOW/concrete.ace")==plannedLeaf(connected,"ROUTES/A/TEXTURES/Concrete.ace"),"season matches coordinated base filename");

        const QString representationsRoot=tmp.path()+"/RepresentationsRoot";
        put(representationsRoot+"/GLOBAL/SHAPES/Tree.S","shape ( images ( 1 image ( Leaf.ACE ) ) )");
        put(representationsRoot+"/GLOBAL/SHAPES/Tree.SD","Shape ( Tree.S )");
        for(const auto &route:{"A","B"})put(representationsRoot+"/ROUTES/"+route+"/route.trk","Tr_RouteFile ( Name ( Route ) )");
        put(representationsRoot+"/ROUTES/A/TEXTURES/leaf.ace","ACE version");
        put(representationsRoot+"/ROUTES/A/TEXTURES/LEAF.DDS","DDS version");
        put(representationsRoot+"/ROUTES/A/TEXTURES/SNOW/leaf.ACE","snow ACE version");
        put(representationsRoot+"/ROUTES/A/TEXTURES/SNOW/leaf.DDS","snow DDS version");
        put(representationsRoot+"/ROUTES/B/TEXTURES/leaf.DDS","DDS-only route");
        put(representationsRoot+"/ROUTES/B/TEXTURES/SNOW/leaf.ace","ACE-only season");
        auto representations=ContentCase::scan(representationsRoot,error);
        check(representations["failures"].toArray().isEmpty() && representations["referenceConflicts"].toArray().isEmpty(),"mixed ACE/DDS route and season combinations coordinate");
        check(edge(representations,"texture-dds-alternative","case-mismatch") && edge(representations,"texture-dds-fallback","case-mismatch"),"DDS inventoried both alongside ACE and when ACE is absent");
        check(plannedLeaf(representations,"GLOBAL/SHAPES/Tree.S")=="Tree.S" && plannedLeaf(representations,"GLOBAL/SHAPES/Tree.SD")=="Tree.sd","uppercase explicit S suffix permits a fixed lowercase SD companion");
        check(plannedLeaf(representations,"ROUTES/A/TEXTURES/leaf.ace")=="Leaf.ACE" &&
            plannedLeaf(representations,"ROUTES/A/TEXTURES/LEAF.DDS")=="Leaf.dds" &&
            plannedLeaf(representations,"ROUTES/A/TEXTURES/SNOW/leaf.DDS")=="Leaf.dds" &&
            plannedLeaf(representations,"ROUTES/B/TEXTURES/SNOW/leaf.ace")=="Leaf.ACE","authored ACE suffix and generated lowercase DDS suffix stay synchronized");
        for(const auto &v:representations["operations"].toArray())check(v.toObject()["operation"]!="edit-reference","DDS projection does not silently lowercase the authored ACE reference");
        check(representations["summary"].toObject()["missingTargetGroups"].toInt()==0,"absent optional texture representation is not missing content");
        put(representationsRoot+"/ROUTES/A/SHAPES/FixedDds.s","shape ( images ( 1 image ( Leaf.DDS ) ) ) )");
        representations=ContentCase::scan(representationsRoot,error);
        check(errorCode(representations,"incompatible-texture-naming-constraints"),"frozen uppercase explicit DDS reference cannot silently violate generated lowercase DDS rule");
        const QString recoveredRoot=tmp.path()+"/RecoveredRoot";
        put(recoveredRoot+"/ROUTES/R/Route.trk","Tr_RouteFile ( Name ( R ) )");
        put(recoveredRoot+"/ROUTES/R/ENVFILES/A.env","world ( terrain_texslot ( Sky.ace ) ) )");
        put(recoveredRoot+"/ROUTES/R/ENVFILES/TEXTURES/sky.ace","texture");
        auto recovered=ContentCase::scan(recoveredRoot,error);
        check(recovered["failures"].toArray().isEmpty(),"syntax warning alone is not a failed case");
        check(recovered["summary"].toObject()["syntaxWarningOnlyDocuments"].toInt()==1,"warning-only document counted separately");
        bool renameSky=false;for(const auto &value:recovered["operations"].toArray()) {
            const auto op=value.toObject();check(op["operation"]!="edit-reference","recovered source reference must stay fixed");
            if(op["to"].toString().endsWith("/Sky.ace"))renameSky=true;
        }
        check(renameSky,"uneditable ENV forces target rename");
        check(command({"--contentcase",recoveredRoot,"--plan"})==0,"syntax-only warning has success exit");
        put(recoveredRoot+"/ROUTES/R/ENVFILES/B.env","world ( terrain_texslot ( SKY.ace ) ) )");
        recovered=ContentCase::scan(recoveredRoot,error);
        bool fixedConflict=false;for(const auto &value:recovered["failures"].toArray())
            if(value.toObject()["code"]=="incompatible-fixed-spellings")fixedConflict=true;
        check(fixedConflict,"two incompatible uneditable references are a real sync failure");
        put(recoveredRoot+"/ROUTES/R/SHAPES/Unreadable.s",malformed);
        recovered=ContentCase::scan(recoveredRoot,error);
        check(recovered["summary"].toObject()["fileReadFailures"].toInt()==1,"decode failure appears on read failure list");
        const auto report=ContentCase::markdownReport(recovered);
        check(report.indexOf("## Errors")<report.indexOf("## Syntax warnings"),"failure list precedes syntax warnings and summary");
        const QString directoryPolicyRoot=tmp.path()+"/DirectoryPolicyRoot";
        put(directoryPolicyRoot+"/routes/RouteMixed/RouteMixed.trk","Tr_RouteFile ( Name ( RouteMixed ) )");
        put(directoryPolicyRoot+"/routes/RouteMixed/world/a.w","Tr_Worldfile ( Static ( FileName ( ../../../track_b/textures/Tree.s ) ) ) )");
        put(directoryPolicyRoot+"/track_b/textures/Tree.s","shape ( images ( 0 ) )");
        put(directoryPolicyRoot+"/routes/RouteMixed/custom/FORESTS.DAT","Forests ( )");
        put(directoryPolicyRoot+"/trains/trainset/ep09/Car.wag","Wagon ( Sound ( ../../common.snd/MixedGroup/lad.sms ) ) )");
        put(directoryPolicyRoot+"/trains/trainset/common.snd/MixedGroup/lad.sms","Tr_SMS ( )");
        check(QDir().mkpath(directoryPolicyRoot+"/trains/trainset/ep09/sound"),"vehicle sound directory fixture");
        auto directoryPolicy=ContentCase::scan(directoryPolicyRoot,error);
        check(directoryPolicy["failures"].toArray().isEmpty(),"frozen shared-folder references remain valid without blanket uppercase");
        bool fixedWorld=false,fixedSound=false,fixedRoute=false;
        for(const auto &value:directoryPolicy["operations"].toArray()) {
            const auto op=value.toObject();
            check(op["operation"]!="edit-reference","consistent custom directory references remain untouched");
            if(op["from"]=="routes/RouteMixed")fixedRoute=op["to"]=="ROUTES/ROUTEMIXED";
            if(op["from"]=="routes/RouteMixed/world")fixedWorld=op["to"]=="ROUTES/ROUTEMIXED/WORLD";
            if(op["from"]=="trains/trainset/ep09/sound")fixedSound=op["to"]=="TRAINS/TRAINSET/ep09/SOUND";
            const auto from=op["from"].toString();
            check(from!="routes/RouteMixed/custom/FORESTS.DAT","catalog basename alone does not imply a fixed application path");
            check(from!="track_b" && from!="track_b/textures" &&
                from!="trains/trainset/ep09" && !from.startsWith("trains/trainset/common.snd"),
                "dynamic directory names are not structural merely because of their basename");
        }
        check(fixedRoute && fixedWorld && fixedSound,"route and fixed directory conventions preserve dynamic vehicle parents");
        put(directoryPolicyRoot+"/trains/trainset/ep09/Long.wag","Wagon ( Sound ( ../SOUND/../../common.snd/MixedGroup/lad.sms ) ) )");
        directoryPolicy=ContentCase::scan(directoryPolicyRoot,error);
        check(directoryPolicy["failures"].toArray().isEmpty(),"longer valid relative spelling is not a frozen-reference error");
        for(const auto &value:directoryPolicy["operations"].toArray())
            check(value.toObject()["operation"]!="edit-reference","valid authored paths need no shortening edits");
        put(directoryPolicyRoot+"/trains/trainset/ep09/Other.wag","Wagon ( Sound ( ../../SharedAudio/lad.sms ) ) )");
        put(directoryPolicyRoot+"/trains/trainset/sharedaudio/lad.sms","Tr_SMS ( )");
        directoryPolicy=ContentCase::scan(directoryPolicyRoot,error);
        check(directoryPolicy["failures"].toArray().isEmpty(),"dynamic directory can satisfy a frozen reference by renaming");
        bool dynamicRename=false;
        for(const auto &value:directoryPolicy["operations"].toArray()) {
            const auto op=value.toObject();
            if(op["from"]=="trains/trainset/sharedaudio")dynamicRename=op["to"]=="TRAINS/TRAINSET/SharedAudio";
        }
        check(dynamicRename,"frozen authored directory case outranks disk spelling");
        put(directoryPolicyRoot+"/trains/trainset/ep09/Conflict.wag","Wagon ( Sound ( ../../SHAREDAUDIO/lad.sms ) ) )");
        bool directoryConflict=false;
        for(const auto &value:ContentCase::scan(directoryPolicyRoot,error)["failures"].toArray())
            if(value.toObject()["code"]=="incompatible-fixed-directory-spellings")directoryConflict=true;
        check(directoryConflict,"incompatible frozen directory references remain errors");
        const QString crossRouteRoot=tmp.path()+"/CrossRouteRoot";
        put(crossRouteRoot+"/ROUTES/Source/Source.trk","Tr_RouteFile ( Name ( Source ) )");
        put(crossRouteRoot+"/ROUTES/Cmk/Cmk.trk","Tr_RouteFile ( Name ( Cmk ) )");
        put(crossRouteRoot+"/ROUTES/Source/WORLD/a.w","Tr_Worldfile ( Static ( FileName ( ../../Cmk/SHAPES/Tree.s ) ) )");
        put(crossRouteRoot+"/ROUTES/Cmk/SHAPES/Tree.s","shape ( images ( 0 ) )");
        auto crossRoutePlan=ContentCase::scan(crossRouteRoot,error);
        bool crossRouteEdit=false;
        for(const auto &value:crossRoutePlan["operations"].toArray()) {
            const auto op=value.toObject();
            if(op["operation"]=="edit-reference" && op["originalLogicalReference"]=="../../Cmk/SHAPES/Tree.s")
                crossRouteEdit=op["proposedLogicalReference"]=="../../CMK/SHAPES/Tree.s";
            check(op["from"]!="ROUTES/Cmk/Cmk.trk","route directory rename does not rename the TRK stem");
        }
        check(crossRouteEdit && crossRoutePlan["failures"].toArray().isEmpty(),"cross-route reference follows uppercase route directory");
        put(crossRouteRoot+"/ROUTES/Source/WORLD/frozen.w","Tr_Worldfile ( Static ( FileName ( ../../Cmk/SHAPES/Tree.s ) ) ) )");
        bool frozenCrossRoute=false;
        for(const auto &value:ContentCase::scan(crossRouteRoot,error)["failures"].toArray())
            if(value.toObject()["code"]=="fixed-reference-cannot-match")frozenCrossRoute=true;
        check(frozenCrossRoute,"uneditable cross-route spelling conflict is reported rather than silently broken");
        const QString precedenceRoot=tmp.path()+"/PrecedenceRoot";
        put(precedenceRoot+"/GLOBAL/tsection.dat","TrackShapes ( TrackShape ( 1 FileName ( Canonical.s ) ) )");
        put(precedenceRoot+"/GLOBAL/SHAPES/canonical.s","shape ( images ( 0 ) )");
        put(precedenceRoot+"/GLOBAL/SHAPES/Other1.sd","Shape ( canonical.s )");
        put(precedenceRoot+"/GLOBAL/SHAPES/Other2.sd","Shape ( canonical.s )");
        bool canonical=false;for(const auto &value:ContentCase::scan(precedenceRoot,error)["operations"].toArray())
            if(value.toObject()["to"]=="GLOBAL/SHAPES/Canonical.s")canonical=true;
        check(canonical,"tsection spelling outranks several editable SD votes");
        const QString scopedRoot=tmp.path()+"/ScopedRoot";
        put(scopedRoot+"/ROUTES/R/R.trk","Tr_RouteFile ( Name ( R ) )");
        put(scopedRoot+"/ROUTES/R/TEXTURES/Bark.ace","unrelated route texture");
        put(scopedRoot+"/ROUTES/R/SHAPES/Body.s","shape ( images ( 0 ) )");
        put(scopedRoot+"/TRAINS/TRAINSET/A/A.eng","Wagon ( WagonShape ( Body.s ) )");
        put(scopedRoot+"/TRAINS/TRAINSET/A/Body.s",s);
        put(scopedRoot+"/TRAINS/TRAINSET/B/Bark.ace","unrelated vehicle texture");
        put(scopedRoot+"/TRAINS/TRAINSET/C/C.eng","Wagon ( WagonShape ( Body.s ) )");
        put(scopedRoot+"/ROUTES/R/VEHICLES/Local/Local.wag","Wagon ( WagonShape ( Body.s ) )");
        put(scopedRoot+"/ROUTES/R/VEHICLES/Local/Body.s",s);
        put(scopedRoot+"/ROUTES/R/WORLD/marker.ws","Tr_Worldsoundfile ( SoundRegion ( FileName ( IMRegionPoint.s ) ) )");
        put(scopedRoot+"/GLOBAL/SHAPES/imregionpoint.s","shape ( images ( 0 ) )");
        const auto scoped=ContentCase::scan(scopedRoot,error);
        check(scoped["failures"].toArray().isEmpty(),"missing content is warning-only");
        check(scoped["summary"].toObject()["missingTargetGroups"].toInt()==3,"only vehicle-local missing resources are warned");
        const auto scopedFiles=scoped["files"].toArray();bool localVehicle=false,localTexture=false,globalMarker=false;
        for(const auto &value:scoped["references"].toArray()) {
            const auto e=value.toObject();const auto source=scopedFiles[e["sourceFileId"].toInt()].toObject()["path"].toString();
            if(source=="ROUTES/R/VEHICLES/Local/Local.wag" && e["kind"]=="shape") {
                localVehicle=scopedFiles[e["targetFileId"].toInt()].toObject()["path"]=="ROUTES/R/VEHICLES/Local/Body.s";
            }
            if(source=="TRAINS/TRAINSET/A/Body.s" && e["kind"]=="texture")
                localTexture=e["status"]=="missing" && e["targetFileId"].toInt()==-1 && e["searchBases"].toArray()==QJsonArray{"TRAINS/TRAINSET/A"};
            if(e["kind"]=="editor-marker-shape")
                globalMarker=scopedFiles[e["targetFileId"].toInt()].toObject()["path"]=="GLOBAL/SHAPES/imregionpoint.s";
        }
        check(localVehicle && localTexture,"vehicle lookups never substitute route or other-vehicle assets");
        check(globalMarker,"sound-region marker uses its explicit global shape role");
        check(command({"--contentcase",scopedRoot,"--plan"})==0,"missing-content warnings do not fail CLI");
        const QString rulesRoot=tmp.path()+"/RulesRoot";
        put(rulesRoot+"/TEMPLATE/ENVFILES/editor.env","world ( terrain_texslot ( Sky.ace ) )");
        put(rulesRoot+"/TEMPLATE/ENVFILES/TEXTURES/Sky.ace","sky");
        put(rulesRoot+"/TEMPLATE/TEXTURES/Bark.ace","route bark");
        put(rulesRoot+"/TEMPLATE/sigcfg.dat","SignalShapes ( _SignalShape ( Post.s ) ) LightTextures ( _LightTex ( Signal Bark.ace ) ) SignalGroupEntries ( Post.s Other.s )");
        put(rulesRoot+"/TEMPLATE/speedpost.dat","Restricted_Shape ( Post.s )");
        put(rulesRoot+"/TEMPLATE/SHAPES/Post.s",s);
        put(rulesRoot+"/TEMPLATE/SHAPES/Other.s","shape ( images ( 0 ) )");
        put(rulesRoot+"/GLOBAL/SHAPES/Unused.s",s);
        put(rulesRoot+"/GLOBAL/TEXTURES/Bark.ace","unrelated template resource");
        put(rulesRoot+"/GLOBAL/objects.ref","malformed ignored catalog ( filename (");
        put(rulesRoot+"/GLOBAL/sigcfg.dat","malformed ignored catalog (");
        put(rulesRoot+"/TRAINS/TRAINSET/A/A.wag","Wagon ( Inside ( PassengerCabinFile ( Inside.s ) ) )");
        put(rulesRoot+"/TRAINS/TRAINSET/A/Inside.s","shape ( images ( 0 ) )");
        put(rulesRoot+"/TRAINS/TRAINSET/A/CABVIEW/A.cvf","Tr_CabViewFile ( FuelCoal ( Coal.ace ) )");
        put(rulesRoot+"/TRAINS/TRAINSET/A/CABVIEW/coal.ace","coal");
        const auto rules=ContentCase::scan(rulesRoot,error);
        check(rules["failures"].toArray().isEmpty(),"template and added filename fields have typed scope");
        int excludedCatalogs=0;for(const auto &v:rules["scopeWarnings"].toArray())excludedCatalogs+=v.toObject()["files"].toInt();
        check(excludedCatalogs==2,"unsupported global catalogs produce scope warnings");
        bool passenger=false,coal=false,globalTexture=false;int signalEdges=0;
        const auto rf=rules["files"].toArray();
        for(const auto &v:rules["references"].toArray()) {
            const auto e=v.toObject();const QString source=rf[e["sourceFileId"].toInt()].toObject()["path"].toString();
            if(e["targetFileId"].toInt()<0)continue;
            const QString target=rf[e["targetFileId"].toInt()].toObject()["path"].toString();
            if(source.endsWith("A.wag"))passenger=target=="TRAINS/TRAINSET/A/Inside.s";
            if(source.endsWith("A.cvf"))coal=target=="TRAINS/TRAINSET/A/CABVIEW/coal.ace";
            if(source=="GLOBAL/SHAPES/Unused.s" && e["kind"]=="texture")globalTexture=target=="TEMPLATE/TEXTURES/Bark.ace";
            if(source=="TEMPLATE/sigcfg.dat")++signalEdges;
        }
        check(passenger && coal && globalTexture && signalEdges==4,"vehicle, cab, all signal entries and global image scopes are correct");
        const QString waterRoot=tmp.path()+"/WaterRoot";
        put(waterRoot+"/TEMPLATE/ENVFILES/water.env","world ( world_water ( world_water_terrain_patch_map ( Wsib-W.raw ) ) )");
        const auto absentWater=ContentCase::scan(waterRoot,error);
        check(absentWater["failures"].toArray().isEmpty() && absentWater["summary"].toObject()["missingTargetReferences"].toInt()==1,
            "globally absent water map is a missing warning without an invented base");
        put(waterRoot+"/Unrelated/Wsib-W.raw","not an authorized lookup location");
        const auto presentWater=ContentCase::scan(waterRoot,error);
        check(!presentWater["failures"].toArray().isEmpty() && !edge(presentWater,"absent-water-patch-map","exact"),
            "basename existence requires a real lookup rule and cannot resolve the water map");
        const QString seasonsRoot=tmp.path()+"/SeasonsRoot";
        put(seasonsRoot+"/TEMPLATE/tiles/Tile.t",t);
        put(seasonsRoot+"/TEMPLATE/TERRTEX/Grass.ace","base");
        put(seasonsRoot+"/TEMPLATE/TERRTEX/SNOW/grass.ace","snow");
        const auto seasons=ContentCase::scan(seasonsRoot,error);
        bool seasonRename=false;
        for(const auto &v:seasons["operations"].toArray()) {
            const auto op=v.toObject();
            if(op["from"]=="TEMPLATE/TERRTEX/SNOW/grass.ace")seasonRename=op["to"]=="TEMPLATE/TERRTEX/SNOW/Grass.ace";
        }
        check(seasonRename,"existing terrain seasonal textures match base filename spelling");
        const QString polesRoot=tmp.path()+"/PolesRoot";
        put(polesRoot+"/TEMPLATE/WORLD/poles.w",binary(block(TS::Tr_Worldfile,
            block(TS::Telepole,block(TS::UiD,word(1))+block(TS::Config,word(0))))));
        put(polesRoot+"/TEMPLATE/telepole.dat","TPoleConfigData ( 1 TPoleConfig ( 0 FileName ( Pole.s ) Shadow ( Shadow.s ) ) )");
        put(polesRoot+"/TEMPLATE/SHAPES/Pole.s",s);
        put(polesRoot+"/TEMPLATE/SHAPES/Shadow.s","shape ( images ( 0 ) )");
        put(polesRoot+"/TEMPLATE/TEXTURES/Bark.ace","pole texture");
        const auto poles=ContentCase::scan(polesRoot,error);
        check(poles["failures"].toArray().isEmpty(),"native Telepole and its route configuration have complete reference coverage");
        int poleShapes=0;
        for(const auto &v:poles["references"].toArray()) {
            const auto e=v.toObject();
            if(e["kind"]=="shape" && e["status"]=="exact" && e["searchBases"].toArray()==QJsonArray{"TEMPLATE/SHAPES"})++poleShapes;
        }
        check(poleShapes==2,"Telepole catalog extracts both pole and shadow shape references");
        const QString customSharedRoot=tmp.path()+"/CustomSharedRoot";
        for(const auto &routeName:{"A","B"}) {
            const QString route=customSharedRoot+"/ROUTES/"+routeName;
            put(route+"/Route.trk","Tr_RouteFile ( Name ( Route ) )");
            put(route+"/WORLD/share.w","Tr_Worldfile ( Static ( FileName ( ../../../track_b/Shared.s ) ) Speedpost ( Speed_Digit_Tex ( Digits.ace ) ) )");
            put(route+"/TEXTURES/Bark.ace",QByteArray(routeName));
            put(route+"/TEXTURES/Digits.ace","digits");
        }
        put(customSharedRoot+"/ROUTES/A/WORLD/binary.w",binary(block(TS::Tr_Worldfile,block(TS::Speedpost,block(TS::Speed_Digit_Tex,string("Digits.ace"))))));
        put(customSharedRoot+"/ROUTES/A/TEXTURES/Copy.trk","Tr_RouteFile ( Name ( NotARoute ) )");
        put(customSharedRoot+"/track_b/Shared.s",s);
        put(customSharedRoot+"/track_b/Unused.s",s);
        put(customSharedRoot+"/track_b/Bark.ace","unrelated asset-directory texture");
        put(customSharedRoot+"/track_b/TILES/Bad.t","broken custom terrain (");
        put(customSharedRoot+"/ROUTES/Custom/Library/Extra.s",s);
        put(customSharedRoot+"/GLOBAL/SHAPES/objects.ref","ignored misplaced catalog (");
        const auto customShared=ContentCase::scan(customSharedRoot,error);
        check(customShared["failures"].toArray().isEmpty(),"custom shared shapes and W speed textures retain valid route contexts");
        const auto sf=customShared["files"].toArray();QSet<QString> sharedTargets;int digits=0;bool ignoredTile=false,ignoredTrk=false,keptExtra=false;
        for(const auto &v:sf) {
            const auto f=v.toObject();
            if(f["path"]=="track_b/TILES/Bad.t")ignoredTile=f["coverage"]=="excluded-by-scope" && !f.contains("sha256");
            if(f["path"]=="ROUTES/A/TEXTURES/Copy.trk")ignoredTrk=f["coverage"]=="excluded-by-scope";
            if(f["path"]=="ROUTES/Custom/Library/Extra.s")keptExtra=f.contains("sha256");
        }
        for(const auto &v:customShared["references"].toArray()) {
            const auto e=v.toObject();if(e["targetFileId"].toInt()<0)continue;
            const QString source=sf[e["sourceFileId"].toInt()].toObject()["path"].toString();
            const QString target=sf[e["targetFileId"].toInt()].toObject()["path"].toString();
            if(source=="track_b/Shared.s" && e["kind"]=="texture")sharedTargets.insert(target);
            if(e["spelling"]=="Digits.ace")++digits;
        }
        check(ignoredTile && ignoredTrk && keptExtra,"exclude custom terrain/descriptors but retain shared shapes");
        check(sharedTargets==QSet<QString>{"ROUTES/A/TEXTURES/Bark.ace","ROUTES/B/TEXTURES/Bark.ace"} && digits==3,"shared S images retain both using routes; text and binary speed textures resolve");
        const QString cabRoot=tmp.path()+"/Cab3dRoot";
        put(cabRoot+"/TRAINS/TRAINSET/Loco/Loco.eng","Wagon ( Include ( ../../common.inc/Cab.inc ) )");
        put(cabRoot+"/TRAINS/common.inc/Cab.inc","ORTS3DCab ( ORTS3DCabFile ( ../../common.cab/Cab/Body.s ) Sound ( ../../common.snd/Cab.sms ) )");
        put(cabRoot+"/TRAINS/TRAINSET/common.cab/Cab/Body.s",s);
        put(cabRoot+"/TRAINS/TRAINSET/common.cab/Cab/Bark.ace","cab texture");
        put(cabRoot+"/TRAINS/TRAINSET/common.snd/Cab.sms","Tr_SMS ( )");
        const auto cabPlan=ContentCase::scan(cabRoot,error);
        check(cabPlan["failures"].toArray().isEmpty(),"3D cab include retains vehicle ownership");
        const auto cf=cabPlan["files"].toArray();bool cabShape=false,cabImage=false,cabSound=false;
        for(const auto &v:cabPlan["references"].toArray()) {
            const auto e=v.toObject();if(e["targetFileId"].toInt()<0)continue;
            const QString target=cf[e["targetFileId"].toInt()].toObject()["path"].toString();
            if(e["kind"]=="shape")cabShape=target=="TRAINS/TRAINSET/common.cab/Cab/Body.s" && e["searchBases"].toArray()==QJsonArray{"TRAINS/TRAINSET/Loco/CABVIEW3D"};
            if(e["kind"]=="texture")cabImage=target=="TRAINS/TRAINSET/common.cab/Cab/Bark.ace";
            if(e["kind"]=="sound-definition")cabSound=target=="TRAINS/TRAINSET/common.snd/Cab.sms";
        }
        check(cabShape && cabImage && cabSound,"3D cab shape, images and sound use their distinct scoped bases");
        put(cabRoot+"/TRAINS/common.inc/Orphan.inc","ORTS3DCab ( ORTS3DCabFile ( Ghost.s ) )");
        const auto orphanCab=ContentCase::scan(cabRoot,error);
        check(edge(orphanCab,"shape","context-unbound"),"ownerless 3D cab include is not resolved from an invented base");
        const QString fallbackRoot=tmp.path()+"/FallbackRoot";
        put(fallbackRoot+"/TRAINS/TRAINSET/A/SOUND/A.sms",
            "Tr_SMS ( File ( ../../B/SOUND/Missing.wav ) File ( ../../B/SOUND/Exists.wav ) File ( Shared.wav ) )");
        put(fallbackRoot+"/TRAINS/TRAINSET/B/SOUND/Exists.wav","expected sound");
        put(fallbackRoot+"/TRAINS/TRAINSET/C/SOUND/Missing.wav","unrelated same-named sound");
        put(fallbackRoot+"/SOUND/Shared.wav","valid root sound fallback");
        const auto fallbacks=ContentCase::scan(fallbackRoot,error);
        check(fallbacks["failures"].toArray().isEmpty(),"escaping fallback cannot override an in-root missing warning");
        check(fallbacks["summary"].toObject()["missingTargetReferences"].toInt()==1,"missing sound has one warning");
        const auto ff=fallbacks["files"].toArray();bool missingScoped=false,existingScoped=false,rootFallback=false;
        for(const auto &v:fallbacks["references"].toArray()) {
            const auto e=v.toObject();
            if(e["spelling"]=="../../B/SOUND/Missing.wav")
                missingScoped=e["status"]=="missing" && e["targetFileId"].toInt()==-1 && e["searchBases"].toArray()==QJsonArray{"TRAINS/TRAINSET/A/SOUND","SOUND"};
            if(e["spelling"]=="../../B/SOUND/Exists.wav" && e["targetFileId"].toInt()>=0)
                existingScoped=ff[e["targetFileId"].toInt()].toObject()["path"]=="TRAINS/TRAINSET/B/SOUND/Exists.wav";
            if(e["spelling"]=="Shared.wav" && e["targetFileId"].toInt()>=0)
                rootFallback=ff[e["targetFileId"].toInt()].toObject()["path"]=="SOUND/Shared.wav" && e["selectedSearchBase"].toInt()==1;
        }
        check(missingScoped && existingScoped && rootFallback,"in-root relative paths and valid fallbacks preserve exact scope");
        check(command({"--contentcase",fallbackRoot,"--plan"})==0,"mixed missing/rejected fallback alone does not fail CLI");
        put(fallbackRoot+"/SOUND/Outside.sms","Tr_SMS ( File ( ../../Outside.wav ) File ( C:/Outside.wav ) File ( \"\" ) )");
        const auto outside=ContentCase::scan(fallbackRoot,error);int externalEdges=0;
        for(const auto &v:outside["references"].toArray())if(v.toObject()["status"]=="external-or-invalid")++externalEdges;
        check(externalEdges==3,"all-escaping, absolute and empty references remain errors");
        check(command({"--contentcase",fallbackRoot,"--plan"})==1,"actual path errors retain scan error exit");
        put(fallbackRoot+"/SOUND/Locomotive.sms","Tr_SMS ( File ( ../../gp38/sound/gp_power_cruise3.wav ) )");
        put(fallbackRoot+"/TRAINS/TRAINSET/gp38/sound/gp_power_cruise3.wav","must not infer vehicle context");
        const auto unsupportedSms=ContentCase::scan(fallbackRoot,error);
        bool explainedSms=false,unresolvedSms=false;
        for(const auto &v:unsupportedSms["failures"].toArray()) {
            const auto f=v.toObject();
            if(f["path"]=="SOUND/Locomotive.sms")explainedSms=f["code"]=="unsupported-sms-location" &&
                f["reason"].toString().contains("leaves the game root") &&
                f["examples"].toArray().first().toString().contains("../gp38/sound/gp_power_cruise3.wav");
        }
        for(const auto &v:unsupportedSms["references"].toArray()) {
            const auto e=v.toObject();
            if(e["spelling"]=="../../gp38/sound/gp_power_cruise3.wav")
                unresolvedSms=e["targetFileId"].toInt()==-1 && e["status"]=="external-or-invalid";
        }
        check(explainedSms && unresolvedSms,"root SOUND locomotive-style path has an understandable error without guessed vehicle lookup");
        QTextStream(stdout)<<"Content-case tests passed\n";return 0;
    } catch(const std::exception &e){QTextStream(stderr)<<e.what()<<"\n";return 1;}
}
