#include <tsre/tests/ContentPathTestSuite.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/Game.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>
#include <tsre/texture/ImageLib.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Traffic.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Activity.h>
#include <tsre/sound/MstsSoundDefinition.h>
#include <tsre/world/TerrainSeason.h>
#include <tsre/world/Trk.h>
#include <settings/SettingsAccess.h>
#include <QTemporaryDir>
#include <QScopedValueRollback>
#include <QImage>
#include <QProcess>
#include <QElapsedTimer>

int TsreTests::runContentPathSuite(bool verbose) {
    TokenTest::Suite test{"[tests:content-path]",verbose};
    QTemporaryDir temp;
    test.check(temp.isValid(),"temporary gameroot");
    if(!temp.isValid()) return test.finish();
#ifdef Q_OS_WIN
    // Optional test-only NTFS capability. Never change an installation or a
    // workspace directory, and never infer runtime policy from this probe.
    QProcess fsutil;
    fsutil.start("fsutil",{"file","setCaseSensitiveInfo",temp.path(),"enable"});
    fsutil.waitForFinished(5000);
#endif
    auto put=[&](const QString &path,const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        test.check(file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size(),"write fixture "+path);
    };
    const QString root=temp.path()+"/MixedGameRoot";
    const QString a=root+"/ROUTES/ALPHA", b=root+"/ROUTES/BETA";
    QDir().mkpath(a+"/TEXTURES"); QDir().mkpath(b+"/TEXTURES");
    QDir().mkpath(root+"/TRAINS/TRAINSET/MixedLoco");
    QDir().mkpath(root+"/TRAINS/CONSISTS");
    put(root+"/GLOBAL/tsection.dat","fixture");
    QScopedValueRollback<QString> gameRoot(Game::root,root);
    QScopedValueRollback<QString> gameRoute(Game::route,"ALPHA");
    QScopedValueRollback<QString> trkStem(Game::trkName);
    QScopedValueRollback<QString> trkFilename(Game::trkFileName);
    QScopedValueRollback<QString> season(Game::season,"Summer");
    QScopedValueRollback<bool> writes(Game::writeEnabled,true);
    QScopedValueRollback<bool> legacyFlag(Game::caseInsensitiveFS,true);
    test.check(Game::checkRoot(root) && Game::checkCERoot(root),"direct-access uppercase directories and lowercase catalog");
    put(a+"/Mixed.Descriptor.TRK","SIMISA@@@@@@@@@@JINX0r0t______\r\nTr_RouteFile ( FileName ( MixedRouteStem ) )");
    test.check(Game::checkRoute("ALPHA") && Game::trkName=="Mixed.Descriptor"
            && Game::trkFileName=="Mixed.Descriptor.TRK","route discovery retains full filename and dotted stem");
    Trk routeDescriptor;routeDescriptor.load();
    test.check(routeDescriptor.routeName=="MixedRouteStem","route descriptor opens with discovered suffix and preserves FileName stem");
    test.check(ContentPath::normalize("//Server/Share//Mixed\\File.S")=="//Server/Share/Mixed/File.S","UNC and authored spelling survive normalization");
    test.check(ContentPath::withExtension("Dir.With.Dot/Tree.S","sd")=="Dir.With.Dot/Tree.sd","uppercase shape implies lowercase sd");
    test.check(ContentPath::key(root+"/Mixed/../Leaf.S")==ContentPath::key(root+"/leaf.s"),"case-independent normalized logical key");
    test.check(ContentPath::normalize("gltfimg:ABC/Hash")=="gltfimg:ABC/Hash"
            && ContentPath::key("gltfimg:ABC")!=ContentPath::key("gltfimg:abc"),"embedded texture identity remains exact");

    const QString shapePath=root+"/GLOBAL/SHAPES/Tree.S";
    put(shapePath,"SIMISA@@@@@@@@@@JINX0s1t______\r\nshape ( )");
    put(root+"/GLOBAL/SHAPES/Tree.sd","SIMISA@@@@@@@@@@JINX0t1t______\r\nShape ( Tree.S ESD_Detail_Level ( 7 ) )");
    const bool caseSensitive=!QFileInfo(root+"/GLOBAL/SHAPES/tree.s").exists();
    qInfo()<<"[tests:content-path] actual fixture filesystem case-sensitive:"<<caseSensitive;
    ShapeLib shapes;
    const int sa=shapes.addShape(shapePath,a+"/TEXTURES");
    test.check(shapes.shape[sa]->getPathId()==shapePath,"shape I/O path is preserved with legacy flag enabled");
    Game::caseInsensitiveFS=false;
    test.check(shapes.addShape(shapePath,a+"/TEXTURES")==sa,"legacy flag cannot change cache identity");
    test.check(shapes.addShape(shapePath.toLower(),(a+"/TEXTURES").toLower())==sa,"shape and texture context keys ignore case");
    test.check(shapes.addShape(shapePath,b+"/TEXTURES")!=sa,"shared shape keeps route texture context");
    Game::season="Snow";
    test.check(shapes.addShape(shapePath,a+"/TEXTURES")!=sa,"season change cannot reuse stale texture context");
    Game::season="Summer";
    if(caseSensitive) {
        test.check(shapes.addShape(root+"/GLOBAL/SHAPES/tree.s",a+"/TEXTURES")==sa,"shape cache matches filename case variants");
        put(root+"/GLOBAL/SHAPES/tree.s","distinct shape");
        test.check(shapes.addShape(root+"/GLOBAL/SHAPES/tree.s",a+"/TEXTURES")==sa,"case-only files share one logical runtime shape identity");
    }
    SFileComplex metadata(shapePath,"Tree.S",a+"/TEXTURES");
    metadata.loadData();
    test.check(metadata.isLoaded() && metadata.getEsdDetailLevel()==7,"real shape loader reads Tree.sd for Tree.S");

    QScopedValueRollback<bool> syncImages(ImageLib::IsThread,false);
    const QString imagePath=a+"/TEXTURES/MixedLeaf.png";
    QImage pixels(2,2,QImage::Format_RGB32);pixels.fill(Qt::red);
    test.check(pixels.save(imagePath),"PNG fixture");
    const int imageId=TexLib::addTex(imagePath);
    test.check(TexLib::mtex[imageId]->loaded && TexLib::mtex[imageId]->pathid==imagePath,"actual image loader preserves name");
    test.check(TexLib::addTex(a+"/TEXTURES",QStringLiteral("MixedLeaf.png"))==imageId,"texture overloads share identity");
    if(caseSensitive) test.check(TexLib::getTex(a+"/TEXTURES/mixedleaf.png")==imageId,"texture cache matches filename case variants");
    const QString late=a+"/TEXTURES/Late.png";
    const int failed=TexLib::addTex(late);
    test.check(!TexLib::mtex[failed]->loaded && pixels.save(late),"missing-first image fixture");
    const int recovered=TexLib::addTex(late);
    test.check(recovered!=failed && TexLib::mtex[recovered]->loaded,"failed image cannot poison later successful request");
    const QString ace=a+"/TEXTURES/Leaf.ACE", dds=a+"/TEXTURES/Leaf.dds";
    put(dds,"DDS fixture");
    auto *cachedDds=new Texture(dds);cachedDds->loaded=true;
    const int ddsId=TexLib::addTex(cachedDds);
    test.check(TexLib::getTex(ace)==ddsId,"implicit DDS fallback uses lowercase suffix");
    put(ace,"ACE fixture");
    test.check(TexLib::getTex(ace)==-1,"existing ACE wins over a cached DDS alias");
    auto *cachedAce=new Texture(ace);cachedAce->loaded=true;
    const int aceId=TexLib::addTex(cachedAce);
    test.check(aceId!=ddsId && TexLib::getTex(ace)==aceId && TexLib::getTex(dds)==ddsId,"ACE and DDS retain distinct loaded representations");
    cachedAce->hashid.clear();
    test.check(TexLib::getTex(ace)==-1,"retired bake aliases are not reused by physical path");
    auto *generated=new Texture("terrain-proc:private:Fixture");generated->loaded=true;
    const int generatedId=TexLib::addTex(generated);
    generated->hashid={"terrain-proc:v1:PublishedHash"};
    test.check(TexLib::getTex("terrain-proc:v1:PublishedHash")==generatedId,
            "published terrain content key can alias a private generated texture");

    put(a+"/TERRTEX/SPRING/Soil.ace","texture");
    test.check(TerrainSeason::resolve(a+"/TERRTEX","SpringRain","Soil.ace",false)==a+"/TERRTEX/SPRING/Soil.ace","semantic rain fallback retains exact filename");
    if(caseSensitive) test.check(TerrainSeason::resolve(a+"/TERRTEX","SpringRain","soil.ace",false).isEmpty(),"seasonal lookup performs no case search");

    QDir().mkpath(a+"/SERVICES");QDir().mkpath(b+"/SERVICES");
    const int srvA=ActLib::AddService(a+"/SERVICES/","Local.srv",true);
    const int srvB=ActLib::AddService(b+"/SERVICES","Local.srv",true);
    test.check(srvA!=srvB && ActLib::GetServiceByName("LOCAL",a)==ActLib::Services[srvA]
            && ActLib::GetServiceByName("local",b)==ActLib::Services[srvB],"logical service names are route-scoped, including trailing slash");
    test.check(ActLib::AddService(a+"/SERVICES","Local.srv",true)==srvA,"unsaved service retains editor identity");
    ActLib::Services[srvA]->save();
    test.check(QFileInfo(a+"/SERVICES/Local.srv").isFile(),"new service saves with preserved stem in structural directory");
    QDir().mkpath(a+"/TRAFFIC");QDir().mkpath(b+"/TRAFFIC");
    const int trA=ActLib::AddTraffic(a+"/TRAFFIC","Local.trf",true);
    const int trB=ActLib::AddTraffic(b+"/TRAFFIC","Local.trf",true);
    test.check(ActLib::GetTrafficByName("LOCAL",a)==ActLib::Traffics[trA]
            && ActLib::GetTrafficByName("local",b)==ActLib::Traffics[trB],"traffic names are route-scoped");
    auto *pa=new Path(a+"/PATHS","Local.pat",true);pa->trPathName="Local";
    auto *pb=new Path(b+"/PATHS","Local.pat",true);pb->trPathName="Local";
    const int paId=ActLib::jestpath++,pbId=ActLib::jestpath++;
    ActLib::Paths[paId]=pa;ActLib::Paths[pbId]=pb;
    test.check(ActLib::GetPathByName("LOCAL",a)==pa && ActLib::GetPathByName("local",b)==pb,"path logical names are route-scoped");

    const QString vehicle=root+"/TRAINS/TRAINSET/MixedLoco";
    put(vehicle+"/CabData.inc","WagonShape ( MixedBody.S )");
    put(vehicle+"/Mixed.eng","SIMISA@@@@@@@@@@JINX0D0t______\r\nWagon ( Mixed Include ( CabData.inc ) )");
    EngLib engines;
    const int engId=engines.addEng(vehicle,"Mixed.eng");
    test.check(engines.eng[engId]->loaded==1 && engines.eng[engId]->pathid==vehicle+"/Mixed.eng","vehicle directory and engine spelling survive loading");
    test.check(engines.eng[engId]->shape.name=="MixedBody.S","engine include retains authored filename and shape spelling");
    test.check(engines.addEng(vehicle,"Mixed.eng")==engId,"engine cache reuses a valid source");
    test.check(engines.addEng(vehicle.toLower(),"MIXED.ENG")==engId &&
            engines.getEngByPathid((vehicle+"/MIXED.eng").toUpper())==engId,
            "engine add/get use the same case-insensitive stored identity");
    Eng engineCopy(engines.eng[engId]);
    test.check(engineCopy.hashid==engines.eng[engId]->hashid,"engine copies retain logical identity");
    if(Settings::boolean("core.content.loading.preferOpenRailsEng")) {
        put(vehicle+"/OPENRAILS/Mixed.eng","SIMISA@@@@@@@@@@JINX0D0t______\r\nWagon ( Mixed WagonShape ( OverrideBody.S ) )");
        test.check(engines.addEng(vehicle,"Mixed.eng")==engId,
                "engine cache keeps its selected source until reload");
        engines.removeAll();
        const int overrideId=engines.addEng(vehicle,"Mixed.eng");
        test.check(engines.eng[overrideId]->shape.name=="OverrideBody.S",
                "explicit engine reload selects newly available Open Rails override");
    }
    const int mixedId=engines.getEngByPathid(vehicle+"/Mixed.eng");
    const int oldRef=engines.eng.at(mixedId)->ref;
    const auto oldCount=engines.eng.size();
    test.check(engines.getEngByPathid(vehicle+"/Missing.eng")==-1
            && engines.eng.size()==oldCount && engines.eng.at(mixedId)->ref==oldRef,
            "indexed engine misses and get do not insert entries or change references");
    test.check(engines.addEng(vehicle,"MIXED.ENG")==mixedId
            && engines.eng.at(mixedId)->ref==oldRef+1,
            "indexed engine add increments the existing reference count");
    const int missingEngine=engines.addEng(vehicle,"Late.eng");
    test.check(engines.eng.at(missingEngine)->loaded!=1
            && engines.getEngByPathid(vehicle+"/Late.eng")==-1,
            "failed engine is not returned from the index");
    put(vehicle+"/Late.eng","SIMISA@@@@@@@@@@JINX0D0t______\r\nWagon ( Late WagonShape ( Late.S ) )");
    const int recoveredEngine=engines.addEng(vehicle,"Late.eng");
    test.check(recoveredEngine!=missingEngine && engines.eng.at(recoveredEngine)->loaded==1
            && engines.getEngByPathid(vehicle+"/LATE.ENG")==recoveredEngine,
            "successful retry replaces a failed engine index entry");
    const int stillMissing=engines.addEng(vehicle,"StillMissing.eng");
    engines.removeBroken();
    test.check(engines.eng.at(missingEngine)==nullptr && engines.eng.at(stillMissing)==nullptr
            && engines.getEngByPathid(vehicle+"/LATE.ENG")==recoveredEngine
            && engines.getEngByPathid(vehicle+"/Mixed.eng")==mixedId,
            "removeBroken preserves the newer successful entry for the same key");
    put(vehicle+"/StillMissing.eng","SIMISA@@@@@@@@@@JINX0D0t______\r\nWagon ( Later )");
    const int afterRemoval=engines.addEng(vehicle,"StillMissing.eng");
    test.check(afterRemoval!=stillMissing && engines.getEngByPathid(vehicle+"/STILLMISSING.ENG")==afterRemoval,
            "removed failed engine can be loaded and indexed later");
    engines.removeAll();
    test.check(engines.jesteng==0 && engines.eng.empty()
            && engines.getEngByPathid(vehicle+"/Mixed.eng")==-1
            && engines.getEngByPathid(vehicle+"/Late.eng")==-1,
            "removeAll clears engine identities as well as numeric IDs");
    const int reusedId=engines.addEng(vehicle,"Late.eng");
    test.check(reusedId==0 && engines.getEngByPathid(vehicle+"/LATE.ENG")==0
            && engines.getEngByPathid(vehicle+"/Mixed.eng")==-1,
            "reused numeric ID cannot be reached through a stale engine key");
    const int act=ActLib::AddAct(a+"/ACTIVITIES","Mixed.act",true);
    test.check(ActLib::AddAct((a+"/ACTIVITIES").toLower(),"MIXED.ACT",true)==act &&
            ActLib::GetAct((a+"/ACTIVITIES").toLower(),"MIXED.ACT")==act,
            "unsaved activity identity ignores filename case");
    test.check(ActLib::AddService((a+"/SERVICES").toLower(),"LOCAL.SRV",true)==srvA &&
            ActLib::AddTraffic((a+"/TRAFFIC").toLower(),"LOCAL.TRF",true)==trA &&
            ActLib::AddPath((a+"/PATHS").toLower(),"LOCAL.PAT")==paId,
            "service traffic and path caches ignore filename case");
    ActLib::Services[srvA]->setNameId("Renamed");
    test.check(ActLib::AddService(a+"/SERVICES","RENAMED.SRV",true)==srvA,
            "service rename refreshes hashid");
    ActLib::Act[act]->setFileName("Renamed");
    test.check(ActLib::GetAct(a+"/ACTIVITIES","RENAMED.ACT")==act,
            "activity rename refreshes hashid");
    auto *unsavedCon=new Consist();unsavedCon->setFileName("NewConsist");
    const int conId=ConLib::jestcon++;ConLib::con[conId]=unsavedCon;
    test.check(ConLib::addCon((root+"/TRAINS/CONSISTS").toLower(),"NEWCONSIST.CON")==conId,
            "unsaved consist cached identity ignores case");
    unsavedCon->setFileName("RenamedConsist");
    test.check(ConLib::addCon(root+"/TRAINS/CONSISTS","RENAMEDCONSIST.CON")==conId,
            "consist rename refreshes hashid");
    put(a+"/SOUND/Mixed.sms","SIMISA@@@@@@@@@@JINX0t1t______\nTr_SMS ( )");
    const int sms=MstsSoundDefinition::AddDefinition(a+"/SOUND","Mixed.sms");
    test.check(MstsSoundDefinition::AddDefinition((a+"/SOUND").toLower(),"MIXED.SMS")==sms,
            "SMS definitions reuse stored case-insensitive identity");
    EngLib lookupBenchmark;
    QVector<QString> requests;
    bool fixturesWritten=true, insertionIdsMatch=true;
    for(int i=0;i<3597;++i) {
        const QString filename="Benchmark"+QString::number(i)+".eng";
        QFile fixture(vehicle+"/"+filename);
        const QByteArray bytes="SIMISA@@@@@@@@@@JINX0D0t______\r\nWagon ( Benchmark )";
        fixturesWritten &= fixture.open(QIODevice::WriteOnly) && fixture.write(bytes)==bytes.size();
        fixture.close();
        insertionIdsMatch &= lookupBenchmark.addEng(vehicle,filename)==i;
        requests.push_back((vehicle+"/"+filename).toUpper());
    }
    test.check(fixturesWritten && insertionIdsMatch,"3597 engine fixtures register through production addEng");
    QElapsedTimer lookupTimer;lookupTimer.start();
    bool lookupIdsMatch=true;
    for(int i=0;i<requests.size();++i)
        lookupIdsMatch &= lookupBenchmark.getEngByPathid(requests[i])==i;
    qInfo()<<"[tests:content-path] 3597 indexed EngLib lookups ms:"<<lookupTimer.elapsed();
    test.check(lookupIdsMatch,"indexed engine lookups retain IDs across 3597 mixed-case requests");
    bool duplicateIdsMatch=true;
    for(int i=0;i<requests.size();++i) {
        const QFileInfo request(requests[i]);
        duplicateIdsMatch &= lookupBenchmark.addEng(request.path(),request.fileName())==i;
    }
    test.check(duplicateIdsMatch && lookupBenchmark.jesteng==3597,
            "3597 indexed duplicate adds preserve IDs and library size");
    for(auto &entry:lookupBenchmark.eng) delete entry.second;
    lookupBenchmark.removeAll();
    put(root+"/TRAINS/CONSISTS/Mixed.con","fixture");
    ConLib::loadSimpleList(root,true);
    test.check(ConLib::conFileList.contains(root+"/TRAINS/CONSISTS/Mixed.con"),"consist discovery preserves spelling");
    QDir().mkpath(root+"/OtherRoot/TRAINS/CONSISTS");
    ConLib::loadSimpleList(root+"/OtherRoot",false);
    test.check(ConLib::conFileList.isEmpty(),"consist discovery cache is scoped to gameroot");
    return test.finish();
}
