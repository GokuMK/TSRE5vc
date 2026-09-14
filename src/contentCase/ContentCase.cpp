#include "ContentCase.h"
#include "ContentCaseDocument.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QThreadPool>
#include <algorithm>
#include <future>
#include <memory>

namespace ContentCase {
namespace {
QString normalized(QString s) { return QDir::cleanPath(s.replace('\\','/')); }
QString joined(const QString &base, QString value) {
    value.replace('\\','/');
    return normalized(base.isEmpty() ? value : base+"/"+value);
}
QString parent(const QString &path) { const int p=path.lastIndexOf('/'); return p<0 ? QString() : path.left(p); }
QString leaf(const QString &path) { return path.section('/',-1); }
QString stem(const QString &path) { return QFileInfo(path).completeBaseName(); }
QString relativeLogicalPath(const QString &base,const QString &target) {
    auto from=normalized(base).split('/',Qt::SkipEmptyParts),to=normalized(target).split('/',Qt::SkipEmptyParts);
    from.removeAll(".");to.removeAll(".");int common=0;
    while(common<from.size() && common<to.size() && from[common]==to[common])++common;
    QStringList result;for(int i=common;i<from.size();++i)result<<"..";
    result.append(to.mid(common));return result.join('/');
}
QJsonArray strings(const QStringList &list) { return QJsonArray::fromStringList(list); }
QJsonArray numbers(const QVector<int> &list) { QJsonArray a; for(int n:list)a.append(n); return a; }
QString familyOf(const QString &path) {
    const QString ext=QFileInfo(path).suffix().toLower(), name=leaf(path).toLower();
    static const QSet<QString> types={"s","sd","w","ws","t","eng","wag","con","act","srv","trf",
        "pat","trk","ref","sms","cvf","env","haz"};
    if(types.contains(ext))return ext;
    if(name=="tsection.dat")return "tsection";
    static const QSet<QString> catalogs={"sigcfg.dat","forests.dat","carspawn.dat","ssource.dat",
        "ttype.dat","speedpost.dat","gantry.dat","hazards.dat","telepole.dat"};
    if(catalogs.contains(name))return "catalog";
    if(ext=="inc")return "include";
    return {};
}
QString coverageOf(const QString &path) {
    if(!familyOf(path).isEmpty())return "pending";
    const QString ext=QFileInfo(path).suffix().toLower();
    static const QSet<QString> leaves={"ace","dds","wav","raw","png","jpg","jpeg","bmp","tga","thm"};
    static const QSet<QString> other={"exe","dll","pdf","rtf","htm","html","zip","rar","7z","log","bak","bk","asv","dtbak"};
    if(leaves.contains(ext))return "leaf-resource";
    if(other.contains(ext))return "non-source-or-backup";
    return "unclassified"; // Includes glTF, material catalogs, databases and extension formats.
}
struct Entry {
    QString path, coverage, sha, encoding, offsets;
    QStringList diagnostics, unclassifiedFields;
    qint64 size=0, modified=0;
    bool directory=false, link=false, parsed=false, compressed=false, binary=false, valid=false, excluded=false;
    Document document;
};
struct Edge {
    int source=-1, target=-1;
    int scalarIndex=0, fieldIndex=-1;
    QString field, spelling, kind, status, context, suffix, expectedLeaf, sourceFamily;
    QString authoredTexture;
    QStringList bases;
    QVector<Scalar> literals;
    QVector<int> candidates;
    int priority=-1;
    bool optional=false, implicit=false;
    bool derivedDds=false;
};
struct Job { int file; QString family, resourceBase, route, textureBase; };
struct Failure {
    int fileId=-1, occurrences=0;
    QString category,code,reason;
    QStringList examples;
    QVector<int> edgeIds;
};
class Scanner {
public:
    QString root,error;
    QVector<Entry> files;
    QHash<QString,QVector<int>> index;
    QSet<QString> inventoryBasenames;
    QVector<Edge> edges;
    QVector<Job> jobs;
    QSet<QString> visited;
    QHash<int,QSet<QString>> shapeContexts;
    QMap<QString,int> extensionCounts, coverageCounts, statusCounts;
    QStringList routeRoots, scanDiagnostics;
    QMap<int,QString> plannedNames;
    QHash<QString,QString> fixedDirectories, plannedDirectories;
    QJsonArray referenceConflicts;
    QJsonArray textureNamingGroups;
    QHash<int,QString> textureReferenceNames;
    QHash<int,int> textureGroupIds;
    QJsonArray scopeWarnings;
    QMap<QString,Failure> failures;
    QSet<int> blockedTargets;
    // Bounded independent file reads; graph construction remains ordered on
    // the calling thread. This matters for roots with tens of thousands of WAG/SD files.
    QThreadPool readers;
    QHash<int,std::shared_ptr<std::future<Entry>>> pending;
    std::function<void(const QString &)> progress;
    Scanner() { readers.setMaxThreadCount(4); }
    void fail(int id,const QString &code,const QString &reason,const QString &detail={},int edge=-1,
              const QString &category="filename-sync-failed") {
        auto &f=failures[category+"|"+QString::number(id)+"|"+code];
        f.fileId=id;f.category=category;f.code=code;f.reason=reason;++f.occurrences;
        if(!detail.isEmpty() && f.examples.size()<5 && !f.examples.contains(detail))f.examples<<detail;
        if(edge>=0)f.edgeIds<<edge;
    }
    bool frozen(int id) const {return !files[id].valid || !files[id].document.referenceScanComplete;}
    QString routeContext(const QString &path) const {
        QString found;
        for(const auto &r:routeRoots)if(path.startsWith(r+"/",Qt::CaseInsensitive)&&r.size()>found.size())found=r;
        return found;
    }
    void discoverRoutes() {
        routeRoots.clear();
        for(const auto &e:files)if(e.directory && !e.link && e.path.compare("TEMPLATE",Qt::CaseInsensitive)==0)
            routeRoots<<e.path;
        // A copied TRK in TEXTURES or a custom asset container is not a route.
        for(int id=0;id<files.size();++id) {
            const auto &e=files[id];
            if(e.directory||e.link||QFileInfo(e.path).suffix().compare("trk",Qt::CaseInsensitive)!=0)continue;
            const QString base=parent(e.path);
            if(parent(base).compare("ROUTES",Qt::CaseInsensitive)!=0 && base.compare("TEMPLATE",Qt::CaseInsensitive)!=0)continue;
            parse(id,"trk");
            if(!files[id].document.referenceScanComplete) {files[id].diagnostics<<"Route context not established from an incomplete TRK reference scan";continue;}
            if(!routeRoots.contains(base))routeRoots<<base;
        }
        std::sort(routeRoots.begin(),routeRoots.end());
        QMap<QString,int> exclusions;
        QMap<QString,QStringList> exclusionExamples;
        static const QSet<QString> routeOnly={"w","ws","t","trk","ref","env","haz","catalog","act","srv","trf","pat"};
        for(auto &e:files) {
            if(e.directory||e.link)continue;
            const QString family=familyOf(e.path),route=routeContext(e.path);
            QString reason;
            if(family=="t" && (route.isEmpty() ||
                (parent(e.path).compare(route+"/TILES",Qt::CaseInsensitive)!=0 && parent(e.path).compare(route+"/LO_TILES",Qt::CaseInsensitive)!=0)))
                reason="Terrain tiles outside a recognized route's TILES/LO_TILES are ignored";
            else if(family=="trk" && parent(e.path).compare(route,Qt::CaseInsensitive)!=0)
                reason="Misplaced or custom route descriptor ignored";
            else if(routeOnly.contains(family) && route.isEmpty())
                reason="Route-bound document outside a recognized route ignored; shared shapes and resources remain included";
            else if(family=="tsection" && e.path.compare("GLOBAL/tsection.dat",Qt::CaseInsensitive)!=0 &&
                (route.isEmpty() || (parent(e.path).compare(route,Qt::CaseInsensitive)!=0 && parent(e.path).compare(route+"/OPENRAILS",Qt::CaseInsensitive)!=0)))
                reason="Misplaced track-section catalog ignored";
            if(!reason.isEmpty()) {
                e.excluded=true;e.coverage="excluded-by-scope";
                const QString key=parent(e.path)+"|"+reason;
                exclusions[key]++;
                if(exclusionExamples[key].size()<5)exclusionExamples[key]<<e.path;
            }
        }
        for(auto it=exclusions.cbegin();it!=exclusions.cend();++it) {
            const int split=it.key().indexOf('|');
            scopeWarnings.append(QJsonObject{{"path",it.key().left(split)},
                {"reason",it.key().mid(split+1)+QString(" (%1 files). Examples: ").arg(it.value())+exclusionExamples[it.key()].join(", ")},
                {"files",it.value()},{"examples",strings(exclusionExamples[it.key()])}});
        }
    }
    bool inventory(const QString &relative) {
        const QDir directory(QDir(root).filePath(relative));
        if(!directory.isReadable()) { scanDiagnostics<<"Unreadable directory: "+relative; return false; }
        const auto children=directory.entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System,QDir::Name);
        for(const auto &info:children) {
            Entry e; e.path=relative.isEmpty()?info.fileName():relative+"/"+info.fileName();
            e.link=info.isSymLink() || info.isJunction();
            e.directory=info.isDir();
            // Directory topology is fingerprinted by entries. NTFS can finish
            // updating a newly created directory timestamp after handles close.
            e.size=e.directory?0:info.size();e.modified=e.directory?0:info.lastModified().toMSecsSinceEpoch();
            e.coverage=e.link?"external-link-not-followed":e.directory?"directory":coverageOf(e.path);
            const int id=files.size(); files.push_back(e); index[e.path.toLower()].push_back(id);
            if(!e.directory)inventoryBasenames.insert(info.fileName().toLower());
            if(e.directory && !e.link) {
                inventory(e.path);
            }
        }
        return true;
    }
    static Entry readEntry(Entry e,const QString &root,const QString &family) {
        e.parsed=true;QFile file(QDir(root).filePath(e.path));
        if(e.size>256*1024*1024 || !file.open(QIODevice::ReadOnly)) {
            e.coverage="read-failed"; e.diagnostics<<"Unreadable or exceeds 256 MiB document limit"; return e;
        }
        const auto bytes=file.readAll();
        const QFileInfo after(file);
        if(file.error()!=QFile::NoError || bytes.size()!=e.size || after.lastModified().toMSecsSinceEpoch()!=e.modified) {
            e.coverage="changed-during-scan";e.diagnostics<<"Source read was incomplete or inventory became stale";return e;
        }
        e.sha=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        e.document=inspectDocument(bytes,family);
        e.valid=e.document.valid;e.encoding=e.document.encoding;e.offsets=e.document.offsets;
        e.compressed=e.document.compressed;e.binary=e.document.binary;
        e.diagnostics.append(e.document.diagnostics);
        e.coverage=e.document.referenceScanComplete?(e.valid?"reference-subset":"references-recovered-with-warnings"):
            e.document.decoded?"reference-scan-incomplete":"document-read-failed";
        // Even a syntactically valid source is not certified for every third-party extension.
        return e;
    }
    void prefetch(const Job &job) {
        if(files[job.file].parsed||pending.contains(job.file))return;
        const Entry entry=files[job.file];const QString rootPath=root,family=job.family;
        auto task=std::make_shared<std::packaged_task<Entry()>>([entry,rootPath,family]{return readEntry(entry,rootPath,family);});
        pending.insert(job.file,std::make_shared<std::future<Entry>>(task->get_future()));
        readers.start([task]{(*task)();});
    }
    void parse(int id, const QString &family) {
        if(files[id].parsed)return;
        if(pending.contains(id)) {
            auto result=pending.take(id);files[id]=result->get();
        } else files[id]=readEntry(files[id],root,family);
    }
    bool external(const QString &p) const {
        return p==".." || p.startsWith("../") || QDir::isAbsolutePath(p) || p.contains(':');
    }
    QVector<int> candidates(const QString &p) const {
        QVector<int> result;
        for(int id:index.value(p.toLower())) if(!files[id].directory && !files[id].link)result<<id;
        return result;
    }
    bool crossesLink(const QString &p) const {
        QString component;
        for(const auto &part:p.split('/')) {
            component=component.isEmpty()?part:component+"/"+part;
            for(int id:index.value(component.toLower()))if(files[id].link)return true;
        }
        return false;
    }
    void resolve(Edge &e) {
        e.spelling.replace('\\','/');
        if(e.spelling.isEmpty()) {e.status="empty-reference";return;}
        if(external(e.spelling) && (QDir::isAbsolutePath(e.spelling)||e.spelling.contains(':'))) {
            e.status="external-or-invalid";return;
        }
        bool escaped=false, searchedInside=false;
        for(int base=0;base<e.bases.size();++base) {
            const auto p=joined(e.bases[base],e.spelling);
            if(external(p) || crossesLink(p)) {escaped=true;continue;}
            searchedInside=true;
            const auto ids=candidates(p);
            if(ids.isEmpty())continue;
            e.candidates=ids;e.priority=base;e.expectedLeaf=leaf(p);
            // Even an exact match must report competing case-only physical files.
            if(ids.size()>1) {e.status="case-collision";return;}
            e.target=ids.first();
            if(files[e.target].excluded){e.target=-1;e.status="excluded-target";return;}
            e.status=files[e.target].path==p?"exact":"case-mismatch";
            return;
        }
        // A rejected fallback cannot turn a missing target at a valid scoped
        // location into an external-reference error. No rejected path is opened.
        e.status=escaped && !searchedInside?"external-or-invalid":e.optional?"optional-missing":"missing";
    }
    int add(const Job &job,const Field &f,int value,const QString &kind,const QStringList &bases,
            const QString &suffix={},bool optional=false,QString overrideSpelling={}) {
        if(value>=f.values.size())return -1;
        Edge e;e.source=job.file;e.field=f.location;e.kind=kind;e.bases=bases;e.scalarIndex=value;e.sourceFamily=job.family;
        e.fieldIndex=f.index;
        e.spelling=overrideSpelling.isEmpty()?f.values[value].text:overrideSpelling;
        e.literals=f.values;e.suffix=suffix;e.context=job.route.isEmpty()?job.resourceBase:job.route;
        e.optional=optional;e.implicit=f.values[value].begin<0;
        // These fields are stems in the format, just as in Service/Consist.
        // Do not quietly reinterpret a malformed stem ending in its extension.
        if(!e.spelling.isEmpty() && !suffix.isEmpty())e.spelling+=suffix;
        resolve(e);
        static const QSet<QString> routeFamilies={"w","ws","t","trk","ref","env","haz","catalog"};
        if(e.status!="empty-reference" && job.route.isEmpty() && routeFamilies.contains(job.family)) {
            e.status="context-unbound";e.target=-1;
        }
        if(e.target>=0 && kind=="shape") {
            const QString tex=job.textureBase.isEmpty() ? (job.route.isEmpty()?job.resourceBase:job.route+"/TEXTURES") : job.textureBase;
            shapeContexts[e.target].insert(tex);
        }
        edges.push_back(e);return edges.size()-1;
    }
    void implicit(const Job &job,QString name,QString spelling,QString base,bool optional=true) {
        Field f;f.name=name;f.location="implicit/"+name;f.values.push_back({spelling,-1,-1});
        add(job,f,0,name,{base},{},optional);
    }
    void texture(const Job &job,const Field &f,int value,QString base) {
        if(value>=f.values.size())return;
        // Inventory both representations independently of the runtime preference.
        const int edge=add(job,f,value,"texture",{base});
        if(edge<0)return;
        const QString raw=f.values[value].text;
        edges[edge].authoredTexture=raw;
        const Edge original=edges[edge];
        if(original.status=="context-unbound" || original.status=="external-or-invalid" || original.status=="empty-reference")return;
        auto candidate=[&](const QString &candidateBase,bool dds,const QString &kind) {
            Edge result=original;result.bases={candidateBase};result.target=-1;
            result.candidates.clear();result.priority=-1;result.kind=kind;result.optional=true;
            if(dds){result.spelling=raw.left(raw.size()-4)+".dds";result.derivedDds=true;}
            resolve(result);return result;
        };
        const bool ace=raw.endsWith(".ace",Qt::CaseInsensitive);
        // Negative inventory evidence avoids repeated seasonal probes for DDS
        // names absent everywhere. Positive resolution still uses only the base.
        const bool ddsPossible=ace && inventoryBasenames.contains(leaf(normalized(raw.left(raw.size()-4)+".dds")).toLower());
        if(ddsPossible && (original.target>=0 || original.status=="missing" || original.status=="case-collision")) {
            auto dds=candidate(base,true,"texture-dds-alternative");
            if(dds.target>=0 || dds.status=="case-collision") {
                if(original.status=="missing") {dds.kind="texture-dds-fallback";dds.optional=false;edges[edge]=dds;}
                else edges.push_back(dds);
            }
        }
        // Enumerate existing seasonal resources; absence is not asserted to be an error.
        static const QStringList seasons={"SPRING","AUTUMN","WINTER","SNOW","NIGHT","SPRINGRAIN",
            "SUMMERRAIN","AUTUMNRAIN","WINTERRAIN","SPRINGSNOW","SUMMERSNOW","AUTUMNSNOW","WINTERSNOW"};
        for(const auto &season:seasons) {
            Edge variant=candidate(base+"/"+season,false,"seasonal-texture-candidate");
            if(variant.target>=0 || variant.status=="case-collision")edges.push_back(variant);
            if(ddsPossible) {
                variant=candidate(base+"/"+season,true,"seasonal-texture-candidate");
                if(variant.target>=0 || variant.status=="case-collision")edges.push_back(variant);
            }
        }
    }
    void classify(const Job &job,const Field &f) {
        if(f.values.isEmpty())return;
        const QString &n=f.name,&family=job.family;
        QString compact=n;compact.remove('_');
        const QString route=job.route,base=job.resourceBase;
        auto shape=[&](int i=0){add(job,f,i,"shape",{route.isEmpty()?base:route+"/SHAPES"});};
        if((family=="ref" && n=="description") || ((family=="eng"||family=="wag") && n=="wagon"))return;
        if(n=="orts3dcabfile" && (family=="eng"||family=="wag")) {
            auto cab=job;
            const QString cabBase=base+"/CABVIEW3D";
            cab.textureBase=parent(joined(cabBase,f.values[0].text));
            add(cab,f,0,"shape",{cabBase});return;
        }
        if(family=="include" && (n=="orts3dcabfile" || (n=="sound" && f.parents.contains("orts3dcab")))) {
            const int i=add(job,f,0,n=="sound"?"sound-definition":"shape",{});
            if(i>=0){edges[i].status="context-unbound";edges[i].context="3D cab include has no discovered vehicle owner";}
            return;
        }
        if(n=="include") {
            QStringList roots{parent(files[job.file].path)};
            if(!roots.contains(base))roots<<base;
            if(roots.first().endsWith("/OPENRAILS",Qt::CaseInsensitive))roots<<parent(roots.first());
            const int i=add(job,f,0,"include",roots);
            if(i>=0 && edges[i].target>=0)jobs.push_back({edges[i].target,family,base,route,job.textureBase});
            return;
        }
        if(family=="cvf" && n=="enginedata")return; // Cab metadata, not a consist vehicle reference.
        if((family=="con" || family=="act") && (n=="enginedata" || n=="wagondata") && f.values.size()>=2) {
            add(job,f,0,n,{"TRAINS/TRAINSET"},n=="enginedata"?".eng":".wag",false,
                f.values[1].text+"/"+f.values[0].text);return;
        }
        if(n=="esd_alternative_texture")return; // Flag, never a filename.
        if(family=="s" && n=="image") {texture(job,f,0,job.textureBase);return;}
        if(family=="sd" && n=="shape") {add(job,f,0,"sd-shape-name",{parent(files[job.file].path)});return;}
        if(family=="t") {
            if(n=="terrain_texslot") {texture(job,f,0,route+"/TERRTEX");return;}
            if(n.startsWith("terrain_sample_") || n=="tsreterrainmaterialbuffer") {
                add(job,f,0,"terrain-sample",{parent(files[job.file].path)});return;
            }
        }
        if(family=="tsection" && n=="filename" && f.parents.contains("trackshape")) {
            if(route.isEmpty()) {
                for(const auto &r:routeRoots) {
                    auto scoped=job;scoped.route=r;scoped.textureBase=r+"/TEXTURES";
                    add(scoped,f,0,"shape",{"GLOBAL/SHAPES"});
                }
                if(routeRoots.isEmpty())add(job,f,0,"shape",{"GLOBAL/SHAPES"});
            } else add(job,f,0,"shape",{"GLOBAL/SHAPES"});
            return;
        }
        if(family=="trk") {
            if(n=="filename") {
                add(job,f,0,"route-database-stem",{route},".tdb");
                for(const auto &suffix:{".rdb",".tit",".rit",".ref"})add(job,f,0,"route-companion-stem",{route},suffix,true);
                return;
            }
            if(f.parents.contains("environment")) {add(job,f,0,"environment",{route+"/ENVFILES"});return;}
            if(n=="graphic"||n=="loadingscreen") {texture(job,f,0,route);return;}
            if(n.startsWith("default")&&n.endsWith("sms")) {add(job,f,0,"sound-definition",{route+"/SOUND","SOUND"});return;}
        }
        if(family=="srv" && n=="train_config") {add(job,f,0,"consist",{"TRAINS/CONSISTS"},".con");return;}
        if((family=="srv"||family=="act") && n=="pathid") {add(job,f,0,"path",{route+"/PATHS"},".pat");return;}
        if((family=="act"||family=="trf") && (n=="service_definition"||n=="player_service_definition")) {
            add(job,f,0,"service",{route+"/SERVICES"},".srv");return;
        }
        if(family=="act" && n=="traffic_definition") {add(job,f,0,"traffic",{route+"/TRAFFIC"},".trf");return;}
        if(family=="eng"||family=="wag") {
            if(n=="wagonshape"||n=="freightanim"||n=="shape"||n=="passengercabinfile") {add(job,f,0,"shape",{base});return;}
            if(n=="sound") {add(job,f,0,"sound-definition",{base+"/SOUND",base,"SOUND"});return;}
            if(n=="cabview") {add(job,f,0,"cabview",{base+"/CABVIEW"});return;}
            if(n=="thumbnail") {texture(job,f,0,base);return;}
        }
        if(family=="sms" && n=="file") {
            QStringList roots{parent(files[job.file].path)};
            if(!route.isEmpty())roots<<route+"/SOUND";
            roots<<"SOUND";add(job,f,0,"sound-sample",roots);return;
        }
        // Includes inherit the owning CVF's directory; the include's physical
        // directory only controls nested Include lookup, not cab textures.
        if(family=="cvf" && (n=="graphic"||n=="cabviewfile"||n=="cabviewwindowfile"||n=="fuelcoal")) {texture(job,f,0,base);return;}
        if(family=="env" && (n=="terrain_texslot"||n=="filename"||n=="texture")) {
            texture(job,f,0,route.isEmpty()?parent(files[job.file].path)+"/TEXTURES":route+"/ENVFILES/TEXTURES");return;
        }
        if(family=="env" && n=="world_water_terrain_patch_map") {
            // Some ENV files put a numeric map index before the RAW filename.
            // Select the resource scalar, retaining its original source offset.
            int value=-1;
            for(int i=0;i<f.values.size();++i)if(f.values[i].text.endsWith(".raw",Qt::CaseInsensitive)) {
                if(value>=0){value=-1;break;} // Ambiguous: keep the unclassified-field error.
                value=i;
            }
            // Negative inventory evidence can prove absence without inventing a
            // lookup base. Never use a basename match to resolve this reference.
            const bool present=value>=0 && inventoryBasenames.contains(leaf(normalized(f.values[value].text)).toLower());
            if(value>=0 && !present && !external(normalized(f.values[value].text))) {
                add(job,f,value,"absent-water-patch-map",{});return;
            }
            // A present candidate still requires a typed rule; fall through to
            // the unclassified-field failure instead of choosing its location.
        }
        if(n=="treetexture") {texture(job,f,0,route+"/TEXTURES");return;}
        if(family=="w"||family=="ws"||family=="ref"||family=="haz") {
            if((family=="w"||family=="ws") && compact=="speeddigittex") {texture(job,f,0,route+"/TEXTURES");return;}
            if(family=="haz"&&n=="workers") {shape();return;}
            if(n=="filename") {
                if(f.parents.contains("trackobj")||f.parents.contains("track"))add(job,f,0,"shape",{"GLOBAL/SHAPES"});
                else if(f.parents.contains("soundregion"))add(job,f,0,"editor-marker-shape",{"GLOBAL/SHAPES"});
                else if(f.parents.contains("soundsource"))add(job,f,0,"sound-definition",{route+"/SOUND","SOUND"});
                else if(f.parents.contains("transfer"))texture(job,f,0,route+"/TEXTURES");
                else if(f.values[0].text.endsWith(".haz",Qt::CaseInsensitive))add(job,f,0,"hazard",{route});
                else shape();
                return;
            }
            if(n=="sound"||n=="ortssoundfilename"||n=="ortscranesound") {
                add(job,f,0,"sound-definition",{route+"/SOUND","SOUND"});return;
            }
        }
        if(family=="catalog") {
            if(f.parents.contains("tpoleconfig") && (n=="filename"||n=="shadow")) {shape();return;}
            if(n=="filename"||n=="carspawneritem"||compact=="signalshape"||compact=="restrictedshape"||compact=="signalgroupentries"||compact.endsWith("signshape")||
               compact=="speedsignshape"||compact=="speedresumeshape"||compact=="milepostshape"||
               compact=="endrestrictedshape"||n=="gantryshape") {
                bool found=false;
                for(int i=0;i<f.values.size();++i) if(f.values[i].text.endsWith(".s",Qt::CaseInsensitive)){shape(i);found=true;}
                if(found)return;
                if(n=="filename") {add(job,f,0,"sound-definition",{route+"/SOUND","SOUND"});return;}
            }
            if(compact=="lighttex"||n=="forest") {
                for(int i=0;i<f.values.size();++i)if(f.values[i].text.endsWith(".ace",Qt::CaseInsensitive)) {texture(job,f,i,route+"/TEXTURES");return;}
            }
            if(compact=="speeddigittex"||compact=="milepostdigittex") {texture(job,f,0,route+"/TEXTURES");return;}
            if(n=="soundregion"||n=="tracktype") {
                for(int i=0;i<f.values.size();++i)if(f.values[i].text.endsWith(".sms",Qt::CaseInsensitive))add(job,f,i,"sound-definition",{route+"/SOUND","SOUND"});
                return;
            }
        }
        for(const auto &v:f.values) if(looksLikeResource(v.text)) {
            auto &d=files[job.file].diagnostics;
            const auto message="Unclassified reference field: "+f.location+" = "+v.text;
            if(!files[job.file].unclassifiedFields.contains(message))files[job.file].unclassifiedFields<<message;
            if(d.size()<50 && !d.contains(message))d<<message;
            return;
        }
    }
    Job seed(int id,QString family) {
        const QString path=files[id].path,route=routeContext(path);
        QString base=parent(path);
        if((family=="eng"||family=="wag") && base.endsWith("/OPENRAILS",Qt::CaseInsensitive))base=parent(base);
        return {id,family,base,route,(route.isEmpty()||family=="eng"||family=="wag")?base:route+"/TEXTURES"};
    }
    void processJobs() {
        for(int i=0;i<jobs.size();++i) {
            for(int next=i;next<std::min(qsizetype(i+8),jobs.size());++next)prefetch(jobs[next]);
            const Job job=jobs[i];
            if(files[job.file].excluded)continue;
            const QString key=QString::number(job.file)+"|"+job.family+"|"+job.resourceBase+"|"+job.route+"|"+job.textureBase;
            if(visited.contains(key))continue;
            visited.insert(key);parse(job.file,job.family);
            // Field storage remains on its source file; include expansion only adds jobs/edges.
            const auto &fields=files[job.file].document.fields;
            for(const auto &f:fields)classify(job,f);
            if(progress && i%2000==0)progress(QString("Reference contexts %1; edges %2").arg(i).arg(edges.size()));
        }
        jobs.clear();
    }
    void extract() {
        for(int id=0;id<files.size();++id) {
            if(files[id].excluded||files[id].directory||files[id].link)continue;
            const auto family=familyOf(files[id].path);
            if(!family.isEmpty() && family!="include" && family!="s")jobs<<seed(id,family);
        }
        processJobs();
        for(int id=0;id<files.size();++id) {
            if(files[id].excluded||files[id].directory||files[id].link)continue;
            if(familyOf(files[id].path)=="include" && !files[id].parsed) {
                auto job=seed(id,"include");jobs<<job;
                files[id].diagnostics<<"Include has no discovered owner; field context is incomplete";
            }
        }
        processJobs();
        for(int id=0;id<files.size();++id) {
            if(files[id].excluded||files[id].directory||files[id].link||familyOf(files[id].path)!="s")continue;
            auto job=seed(id,"s");
            implicit(job,"shape-companion",stem(files[id].path)+".sd",parent(files[id].path));
            auto contexts=shapeContexts.value(id).values();std::sort(contexts.begin(),contexts.end());
            const bool globalShape=files[id].path.startsWith("GLOBAL/SHAPES/",Qt::CaseInsensitive);
            const bool customShared=job.route.isEmpty() && !files[id].path.startsWith("TRAINS/TRAINSET/",Qt::CaseInsensitive);
            if(globalShape || (customShared && contexts.isEmpty())) {
                for(const auto &r:routeRoots)if(!contexts.contains(r+"/TEXTURES"))contexts<<r+"/TEXTURES";
                std::sort(contexts.begin(),contexts.end());
            }
            if(contexts.isEmpty()) {
                if(globalShape || customShared) {
                    files[id].diagnostics<<"Shared shape: route texture context not established";
                    parse(id,"s");
                    for(const auto &field:files[id].document.fields)if(field.name=="image" && !field.values.isEmpty()) {
                        Edge edge;edge.source=id;edge.field=field.location;edge.kind="texture";
                        edge.spelling=field.values[0].text;edge.literals=field.values;
                        edge.status="context-unbound";edge.context="unreferenced-shared-shape";
                        for(const auto &r:routeRoots)edge.bases<<r+"/TEXTURES";
                        edges.push_back(edge);
                    }
                    continue;
                }
                contexts<<job.textureBase;
            }
            for(const auto &context:contexts) {job.textureBase=context;job.route=routeContext(context+"/placeholder");jobs<<job;}
        }
        processJobs();
    }
    QString byteHash(int id) {
        auto &e=files[id];if(!e.sha.isEmpty())return e.sha;
        QFile f(QDir(root).filePath(e.path));if(!f.open(QIODevice::ReadOnly))return {};
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if(!hash.addData(&f))return {};
        const QFileInfo after(f);
        if(after.size()!=e.size || after.lastModified().toMSecsSinceEpoch()!=e.modified)return {};
        e.sha=QString::fromLatin1(hash.result().toHex());return e.sha;
    }
    QJsonArray conflicts() {
        QJsonArray result;QStringList keys=index.keys();std::sort(keys.begin(),keys.end());
        for(const auto &key:keys) {
            QVector<int> ids;for(int id:index.value(key))if(!files[id].excluded)ids<<id;
            if(ids.size()<2)continue;
            bool same=true,dirs=true;QString first;
            for(int id:ids) {
                dirs=dirs && files[id].directory;
                if(files[id].directory||files[id].link){same=false;continue;}
                const QString hash=byteHash(id);
                if(hash.isEmpty())same=false;
                if(first.isEmpty())first=hash;else if(first!=hash)same=false;
            }
            result.append(QJsonObject{{"logicalKey",key},{"fileIds",numbers(ids)},
                {"classification",dirs?"directory-collision":same?"identical-file-candidates":"different-or-unreadable-files"},
                {"decision",same?"propose consolidation only after reference/companion review":"blocked; explicit resolution required"}});
        }
        return result;
    }
    QString plannedDirectory(const QString &path) const {
        if(path.isEmpty() || path==".")return {};
        const QString key=normalized(path).toLower();
        const auto found=plannedDirectories.constFind(key);
        if(found!=plannedDirectories.cend())return found.value();
        // A search base need not exist (for example, a missing SOUND folder
        // before a ../common.snd reference). Apply only known structural roles.
        return joined(plannedDirectory(parent(path)),fixedDirectories.value(key,leaf(path)));
    }
    void planDirectories() {
        auto fixedDir=[&](const QString &path){fixedDirectories.insert(path.toLower(),leaf(path).toUpper());};
        QSet<QString> textureBases={"GLOBAL/TEXTURES"};
        for(const auto &path:{"GLOBAL","ROUTES","TRAINS","SOUND","TEMPLATE",
                "GLOBAL/SHAPES","GLOBAL/TEXTURES","TRAINS/TRAINSET","TRAINS/CONSISTS",
                "TRAINS/CONSISTS/RANDOMSETS"})fixedDir(path);
        for(const auto &route:routeRoots) {
            // Route directory names use the agreed uppercase layout convention,
            // although the application discovers them by enumeration.
            fixedDir(route);
            for(const auto &name:{"SHAPES","TEXTURES","TERRTEX","TILES","LO_TILES","WORLD","TD",
                    "ACTIVITIES","SERVICES","TRAFFIC","PATHS","SOUND","ENVFILES","OPENRAILS",
                    "ADDONS","PROCEDURAL","TRACKPROFILES","TERRAIN_MAPS"})
                fixedDir(route+"/"+name);
            fixedDir(route+"/ENVFILES/TEXTURES");
            textureBases.insert(route+"/TEXTURES");textureBases.insert(route+"/TERRTEX");
            textureBases.insert(route+"/ENVFILES/TEXTURES");
        }
        // Vehicle owners can also be outside TRAINSET. An arbitrary folder
        // merely named sound or textures does not establish a structural role.
        for(int id=0;id<files.size();++id) {
            const auto family=familyOf(files[id].path);
            if(files[id].excluded || files[id].directory || files[id].link || (family!="eng" && family!="wag"))continue;
            const QString owner=seed(id,family).resourceBase;
            for(const auto &name:{"SOUND","CABVIEW","CABVIEW3D","OPENRAILS"})fixedDir(owner+"/"+name);
            textureBases.insert(owner);textureBases.insert(owner+"/CABVIEW");textureBases.insert(owner+"/CABVIEW3D");
        }
        const QStringList seasons={"SPRING","AUTUMN","WINTER","SNOW","NIGHT","SPRINGRAIN",
            "SUMMERRAIN","AUTUMNRAIN","WINTERRAIN","SPRINGSNOW","SUMMERSNOW","AUTUMNSNOW","WINTERSNOW"};
        for(const auto &e:edges)if(e.kind=="texture" || e.kind=="texture-dds-fallback")
            for(const auto &base:e.bases)textureBases.insert(base);
        for(const auto &base:textureBases)for(const auto &season:seasons)fixedDir(base+"/"+season);

        // Only explicit components of an uneditable reference constrain a
        // dynamic directory. The lookup base itself is supplied by the loader.
        QHash<int,QSet<QString>> demands;
        for(const auto &e:edges)if(e.target>=0 && e.priority>=0 && !e.implicit &&
                e.kind!="seasonal-texture-candidate" && frozen(e.source)) {
            QString current=e.bases[e.priority];
            const auto parts=e.spelling.split('/',Qt::SkipEmptyParts);
            for(int i=0;i+1<parts.size();++i) {
                const auto &part=parts[i];current=joined(current,part);
                if(part=="." || part==".." || fixedDirectories.contains(current.toLower()))continue;
                const auto ids=index.value(current.toLower());
                if(ids.size()==1 && files[ids.first()].directory && !files[ids.first()].link)
                    demands[ids.first()].insert(part);
            }
        }
        // Inventory order is parent before child. Preserve discovered dynamic
        // spelling unless a fixed source requires a different consistent name.
        for(int id=0;id<files.size();++id) {
            const auto &e=files[id];if(!e.directory || e.link)continue;
            QString selected=fixedDirectories.value(e.path.toLower(),leaf(e.path));
            const auto required=demands.value(id);
            if(required.size()==1)selected=*required.cbegin();
            if(required.size()>1) {
                auto names=required.values();std::sort(names.begin(),names.end());
                blockedTargets.insert(id);
                fail(id,"incompatible-fixed-directory-spellings","Uneditable references require incompatible dynamic directory names",names.join(" / "));
            }
            const QString to=joined(plannedDirectory(parent(e.path)),selected);
            plannedNames[id]=to;
            if(index.value(e.path.toLower()).size()==1)plannedDirectories.insert(e.path.toLower(),to);
        }
    }
    void coordinateTextures() {
        // Equality constraints are per physical field, across all route contexts,
        // representations and seasons. Shared targets connect these constraints
        // transitively; distinct route files remain distinct file IDs.
        QVector<int> leaders(files.size());
        for(int id=0;id<leaders.size();++id)leaders[id]=id;
        auto leader=[&](int id){while(leaders[id]!=id){leaders[id]=leaders[leaders[id]];id=leaders[id];}return id;};
        QHash<QString,int> fieldTargets;
        QVector<int> textureEdges;
        for(int i=0;i<edges.size();++i) {
            const auto &e=edges[i];if(e.target<0 || e.authoredTexture.isEmpty())continue;
            textureEdges<<i;
            const QString key=QString::number(e.source)+"|"+e.field+"|"+QString::number(e.scalarIndex);
            if(fieldTargets.contains(key)) {
                const int a=leader(fieldTargets.value(key)),b=leader(e.target);
                leaders[std::max(a,b)]=std::min(a,b);
            } else fieldTargets.insert(key,e.target);
        }
        QMap<int,QVector<int>> components;
        for(int edgeId:textureEdges)components[leader(edges[edgeId].target)]<<edgeId;
        for(auto component=components.cbegin();component!=components.cend();++component) {
            QSet<int> members;
            bool derivedDds=false;
            for(int edgeId:component.value()) {
                members.insert(edges[edgeId].target);derivedDds|=edges[edgeId].derivedDds;
            }
            auto ids=members.values();std::sort(ids.begin(),ids.end());
            const QString extension=derivedDds?"ace":QFileInfo(files[ids.first()].path).suffix().toLower();
            QSet<QString> stems,extensions;
            for(int id:ids) {
                stems.insert(stem(files[id].path));
                const QString ext=QFileInfo(files[id].path).suffix();
                if(ext.compare(extension,Qt::CaseInsensitive)==0)extensions.insert(ext);
            }
            for(int edgeId:component.value()) {
                const QString name=normalized(edges[edgeId].authoredTexture);
                stems.insert(stem(name));
                const QString ext=QFileInfo(name).suffix();
                if(ext.compare(extension,Qt::CaseInsensitive)==0)extensions.insert(ext);
            }
            if(extensions.isEmpty())extensions.insert(extension);
            QStringList names;
            for(const auto &name:stems)for(const auto &ext:extensions)names<<name+"."+ext;
            std::sort(names.begin(),names.end());
            auto physicalName=[&](const QString &name,int target) {
                const QString ext=QFileInfo(files[target].path).suffix().toLower();
                if(ext==extension)return name;
                if(extension=="ace" && ext=="dds")return stem(name)+".dds";
                return QString(); // A new representation needs an explicit mapping.
            };
            auto logicalName=[&](const QString &name,const Edge &e) {
                return e.derivedDds?name:physicalName(name,e.target);
            };
            QString selected;
            int bestFixed=-1,bestEdits=0,bestRenames=0;
            for(const auto &name:names) {
                QSet<int> changed,fixedChanged;
                int renames=0;
                for(int id:ids)if(physicalName(name,id)!=leaf(files[id].path))++renames;
                for(int edgeId:component.value()) {
                    const auto &e=edges[edgeId];
                    if(logicalName(name,e)==leaf(normalized(e.authoredTexture)))continue;
                    changed.insert(e.source);
                    if(!e.implicit && frozen(e.source))fixedChanged.insert(e.source);
                }
                if(bestFixed<0 || fixedChanged.size()<bestFixed ||
                    (fixedChanged.size()==bestFixed && (changed.size()<bestEdits ||
                        (changed.size()==bestEdits && renames<bestRenames)))) {
                    selected=name;bestFixed=fixedChanged.size();bestEdits=changed.size();bestRenames=renames;
                }
            }
            bool blocked=bestFixed>0;
            for(int id:ids)if(physicalName(selected,id).isEmpty())blocked=true;
            if(blocked) {
                QStringList demands;
                for(int edgeId:component.value()) {
                    const auto &e=edges[edgeId];
                    if(!e.implicit && frozen(e.source)) {
                        const QString name=leaf(normalized(e.authoredTexture));
                        if(!demands.contains(name))demands<<name;
                    }
                }
                std::sort(demands.begin(),demands.end());
                fail(ids.first(),"incompatible-texture-naming-constraints",
                    "Shared texture names cannot satisfy all frozen references and representation rules",demands.join(" / "));
                for(int id:ids)blockedTargets.insert(id);
            }
            for(int id:ids) {
                const QString name=physicalName(selected,id);
                if(!name.isEmpty())plannedNames[id]=joined(plannedDirectory(parent(files[id].path)),name);
            }
            for(int edgeId:component.value())textureReferenceNames[edgeId]=logicalName(selected,edges[edgeId]);
            if(ids.size()>1 || blocked) {
                const int groupId=textureNamingGroups.size();
                for(int id:ids)textureGroupIds.insert(id,groupId);
                textureNamingGroups.append(QJsonObject{{"id",groupId},{"fileIds",numbers(ids)},
                    {"logicalFilename",selected},{"referenceEdges",component.value().size()},
                    {"sourceFilesRequiringLeafEdits",bestEdits},{"fileRenames",bestRenames},
                    {"decision",blocked?"blocked; incompatible naming constraints":"coordinated; proposal-only"},
                    {"reason","joint naming across shared fields, ACE/DDS representations and existing seasons; preserve distinct assets"}});
            }
        }
    }
    QJsonArray operations() {
        planDirectories();
        QHash<int,QMap<QString,QSet<int>>> votes;
        QHash<int,QMap<QString,QVector<int>>> fixed;
        QHash<int,QSet<QString>> tsectionNames;
        QHash<int,QVector<int>> incoming;
        for(int i=0;i<edges.size();++i) {
            const auto &e=edges[i];
            if(e.target>=0 && !e.expectedLeaf.isEmpty() && e.kind!="seasonal-texture-candidate") {
                votes[e.target][e.expectedLeaf].insert(e.source);incoming[e.target]<<i;
                if(!e.implicit && frozen(e.source))fixed[e.target][e.expectedLeaf]<<i;
                if(e.sourceFamily=="tsection" && e.kind=="shape")tsectionNames[e.target].insert(e.expectedLeaf);
            }
        }
        for(int id=0;id<files.size();++id) {
            const auto &e=files[id];if(e.excluded||e.directory||e.link)continue;
            auto editCost=[&](const QString &candidate){
                QSet<int> changed;for(int edge:incoming.value(id))if(edges[edge].expectedLeaf!=candidate)changed.insert(edges[edge].source);
                return changed.size();
            };
            QString selected=leaf(e.path);auto cost=editCost(selected);
            for(auto it=votes[id].cbegin();it!=votes[id].cend();++it) {
                const auto candidateCost=editCost(it.key());
                if(candidateCost<cost){selected=it.key();cost=candidateCost;}
            }
            // tsection controls global-shape naming ahead of editable SD/world
            // spellings. A source whose references must remain fixed outranks it.
            const auto preferred=tsectionNames.value(id);
            if(!preferred.isEmpty() && !preferred.contains(selected)) {
                auto names=preferred.values();std::sort(names.begin(),names.end());selected=names.first();
                for(const auto &name:names)if(editCost(name)<editCost(selected))selected=name;
            }
            const auto demands=fixed.value(id);
            if(demands.size()==1)selected=demands.firstKey();
            if(demands.size()>1) {
                blockedTargets.insert(id);
                fail(id,"incompatible-fixed-spellings","Uneditable source references require incompatible target filenames",demands.keys().join(" / "));
            }
            const QString family=familyOf(e.path);
            const QString route=routeContext(e.path),base=parent(e.path);
            const bool fixedCatalog=e.path.compare("GLOBAL/tsection.dat",Qt::CaseInsensitive)==0 ||
                (!route.isEmpty() && (base.compare(route,Qt::CaseInsensitive)==0 ||
                    base.compare(route+"/OPENRAILS",Qt::CaseInsensitive)==0));
            if(fixedCatalog && (family=="tsection"||family=="catalog"))selected=leaf(e.path).toLower();
            if(!route.isEmpty()) {
                const QString name=leaf(e.path).toLower();
                if(base.compare(route,Qt::CaseInsensitive)==0
                        && (name=="sigscr.dat" || name=="terrainmaterials.dat" || name=="weathertransitions.dat"))
                    selected=name;
                if(base.compare(route+"/PROCEDURAL",Qt::CaseInsensitive)==0 && name=="shapetemplates.dat")
                    selected=name;
            }
            plannedNames[id]=joined(plannedDirectory(parent(e.path)),selected);
        }
        coordinateTextures();
        // Companion stems follow the chosen S name; no file is renamed in stage 1.
        for(const auto &e:edges)if(e.kind=="shape-companion" && e.target>=0) {
            const auto source=plannedNames.value(e.source);
            plannedNames[e.target]=joined(parent(source),stem(source)+".sd");
        }
        QJsonArray ops;
        for(int id=0;id<files.size();++id) {
            const auto &e=files[id];if(e.excluded||e.link)continue;
            const QString to=plannedNames.value(id,e.path);
            // Parent directory operations carry descendant paths; list leaf renames only.
            if((e.directory && leaf(e.path)!=leaf(to)) || (!e.directory && leaf(e.path)!=leaf(to))) {
                const QString why=e.directory?(fixedDirectories.contains(e.path.toLower())?
                    "fixed application directory convention":"satisfy uneditable directory reference"):
                    (textureGroupIds.contains(id)?"coordinated texture naming / representation convention":
                        "reference spelling votes / companion convention");
                ops.append(QJsonObject{{"operation",e.directory?"rename-directory":"rename-file"},
                    {"fileId",id},{"from",e.path},{"to",to},{"reason",why},
                    {"readiness",blockedTargets.contains(id)?"blocked; incompatible fixed reference spellings":"proposal-only; coverage and collision review required"}});
            }
        }
        QMap<QString,QMap<QString,QVector<int>>> fieldSpellings;
        QHash<QString,int> referenceOperations;
        for(int i=0;i<edges.size();++i) {
            const auto &e=edges[i];if(e.target<0 || e.implicit || e.priority<0 || e.kind=="seasonal-texture-candidate")continue;
            const QString to=plannedNames.value(e.target);
            const QString base=plannedDirectory(e.bases[e.priority]);
            QString spelling=relativeLogicalPath(base,to);
            if(textureReferenceNames.contains(i))spelling=joined(parent(spelling),textureReferenceNames.value(i));
            if(textureReferenceNames.contains(i)) {
                // Prefer repairing authored components to shortening the path;
                // the same field must keep one spelling in every context.
                QString authored=e.authoredTexture;authored.replace('\\','/');
                auto parts=authored.split('/',Qt::SkipEmptyParts);
                QString current=e.bases[e.priority];
                for(int part=0;part+1<parts.size();++part) {
                    current=joined(current,parts[part]);
                    if(parts[part]!="." && parts[part]!="..")parts[part]=leaf(plannedDirectory(current));
                }
                if(!parts.isEmpty())parts.last()=textureReferenceNames.value(i);
                const QString repaired=parts.join('/');
                const QString physical=e.derivedDds?repaired.left(repaired.size()-4)+".dds":repaired;
                if(joined(base,physical)==to)spelling=repaired;
            }
            if(!e.suffix.isEmpty() && spelling.endsWith(e.suffix,Qt::CaseInsensitive))spelling.chop(e.suffix.size());
            QString original=e.authoredTexture.isEmpty()?e.spelling:e.authoredTexture;
            if(!e.suffix.isEmpty() && original.endsWith(e.suffix,Qt::CaseInsensitive))original.chop(e.suffix.size());
            // Retain valid authored relative paths, including paths longer than
            // the shortest spelling. Do not require an edit merely to shorten one.
            if(joined(base,e.spelling)==to &&
                (!textureReferenceNames.contains(i) || leaf(normalized(original))==textureReferenceNames.value(i)))spelling=original;
            const QString fieldKey=QString::number(e.source)+"|"+e.field+"|"+QString::number(e.scalarIndex);
            fieldSpellings[fieldKey][normalized(spelling)]<<i;
            if(normalized(spelling)==normalized(original))continue;
            if(frozen(e.source)) {
                fail(e.source,"fixed-reference-cannot-match","Target naming cannot satisfy this uneditable reference",
                    e.field+": "+original+" requires "+spelling+" ("+e.context+")",i);
                blockedTargets.insert(e.target);
                continue;
            }
            const QString operationKey=fieldKey+"|"+normalized(spelling);
            if(referenceOperations.contains(operationKey)) {
                const int opId=referenceOperations.value(operationKey);auto op=ops[opId].toObject();
                auto ids=op["edgeIds"].toArray();ids.append(i);op["edgeIds"]=ids;ops[opId]=op;
                continue;
            }
            referenceOperations.insert(operationKey,ops.size());
            ops.append(QJsonObject{{"operation","edit-reference"},{"edgeId",i},{"edgeIds",QJsonArray{i}},{"sourceFileId",e.source},
                {"location",e.field},{"originalLogicalReference",original},{"proposedLogicalReference",spelling},
                {"reason","match final filename and directory spelling"},
                {"readiness","requires execution preflight and preservation verification"}});
        }
        QSet<int> blockedEdges;
        for(auto it=fieldSpellings.cbegin();it!=fieldSpellings.cend();++it)if(it.value().size()>1) {
            QJsonArray alternatives;
            for(auto choice=it.value().cbegin();choice!=it.value().cend();++choice) {
                alternatives.append(QJsonObject{{"spelling",choice.key()},{"edgeIds",numbers(choice.value())}});
                for(int id:choice.value())blockedEdges.insert(id);
            }
            const auto &e=edges[it.value().first().first()];
            referenceConflicts.append(QJsonObject{{"sourceFileId",e.source},{"location",e.field},
                {"scalarIndex",e.scalarIndex},{"alternatives",alternatives},
                {"decision","blocked; shared field needs coordinated target spelling across contexts"}});
            fail(e.source,"shared-field-conflict","One source field has incompatible proposals across lookup contexts",e.field);
        }
        for(int i=0;i<ops.size();++i) {
            auto op=ops[i].toObject();
            if(op["operation"]=="edit-reference" && blockedEdges.contains(op["edgeId"].toInt())) {
                op["readiness"]="blocked; conflicting proposals for the same source field";ops[i]=op;
            }
            if(op["operation"]=="edit-reference" && blockedTargets.contains(edges[op["edgeId"].toInt()].target)) {
                op["readiness"]="blocked; target belongs to an unresolved naming component";ops[i]=op;
            }
            if((op["operation"]=="rename-file" || op["operation"]=="rename-directory") && blockedTargets.contains(op["fileId"].toInt())) {
                op["readiness"]="blocked; target cannot satisfy fixed source references";ops[i]=op;
            }
        }
        return ops;
    }
    QJsonArray includeCycles() const {
        QHash<int,QVector<int>> graph;for(const auto &e:edges)if(e.kind=="include"&&e.target>=0)graph[e.source]<<e.target;
        QJsonArray cycles;QHash<int,int> state;
        std::function<void(int,int)> visit=[&](int id,int depth){
            if(depth>256){cycles.append(QJsonObject{{"fileId",id},{"issue","include depth exceeds 256"}});return;}
            state[id]=1;
            for(int next:graph.value(id)) {
                if(state.value(next)==1)cycles.append(QJsonObject{{"sourceFileId",id},{"targetFileId",next},{"issue","include cycle"}});
                else if(!state.value(next))visit(next,depth+1);
            }
            state[id]=2;
        };
        auto keys=graph.keys();std::sort(keys.begin(),keys.end());
        for(int id:keys)if(!state.value(id))visit(id,0);
        return cycles;
    }
    QJsonObject finish() {
        auto collisionList=conflicts();const auto ops=operations();
        QMap<QString,QVector<int>> destinations;
        for(int id=0;id<files.size();++id)if(!files[id].link && !files[id].excluded)
            destinations[plannedNames.value(id).toLower()]<<id;
        for(auto it=destinations.cbegin();it!=destinations.cend();++it)if(it.value().size()>1 && index.value(it.key()).size()!=it.value().size())
            collisionList.append(QJsonObject{{"logicalKey",it.key()},{"fileIds",numbers(it.value())},
                {"classification","proposed-destination-collision"},{"decision","blocked; naming proposals must be reconciled"}});
        for(const auto &message:scanDiagnostics)fail(-1,"inventory-read-failed",message,{},-1,"file-read-failed");
        for(int id=0;id<files.size();++id) {
            const auto &e=files[id];if(!e.parsed)continue;
            if(e.coverage=="read-failed" || e.coverage=="changed-during-scan" || !e.document.decoded)
                fail(id,"source-read-failed","Source bytes could not be read or decoded",e.diagnostics.join("; "),-1,"file-read-failed");
            else if(!e.document.referenceScanComplete)
                fail(id,"reference-discovery-incomplete","Required filename information could not be completely collected",
                    (e.document.discoveryFailures.isEmpty()?e.document.diagnostics:e.document.discoveryFailures).join("; "));
            for(const auto &field:e.unclassifiedFields)
                fail(id,"unclassified-reference-field","Filename-like fields lack a typed lookup rule",field);
        }
        QMap<int,Failure> missingTargets;
        int missingReferences=0;
        for(int i=0;i<edges.size();++i) {
            const auto &e=edges[i];
            if(e.status=="exact"||e.status=="case-mismatch"||e.status=="optional-missing")continue;
            if(e.status=="excluded-target") {
                scopeWarnings.append(QJsonObject{{"path",files[e.source].path},{"reason","Reference points into an excluded scope; preserved without repair: "+e.spelling}});
                continue;
            }
            const QString detail=e.field+": "+e.spelling+" ["+e.context+"]";
            if(e.status=="missing") {
                auto &warning=missingTargets[e.source];warning.fileId=e.source;++warning.occurrences;++missingReferences;
                const QString example=e.field+": "+e.spelling+(e.bases.isEmpty()?
                    "; absent from entire inventory by basename; lookup base unclassified (absence check only)":
                    "; searched "+e.bases.join(", "));
                if(warning.examples.size()<5 && !warning.examples.contains(example))warning.examples<<example;
                warning.edgeIds<<i;
                continue;
            }
            if(e.status=="external-or-invalid" && e.sourceFamily=="sms" &&
                parent(files[e.source].path).compare("SOUND",Qt::CaseInsensitive)==0 &&
                e.spelling.startsWith("../") && !e.bases.isEmpty() &&
                std::all_of(e.bases.cbegin(),e.bases.cend(),[&](const QString &base){return external(joined(base,e.spelling));})) {
                fail(e.source,"unsupported-sms-location",
                    "Unsupported SMS location/reference layout: this relative sound path leaves the game root when resolved from root SOUND",
                    detail+"; resolved relative to game root: "+joined("SOUND",e.spelling)+
                        "; the tool does not infer a locomotive directory or relocate the SMS",i);
                continue;
            }
            fail(e.source,e.status,e.status=="context-unbound"?"Reference lookup context could not be established":
                e.status=="case-collision"?"Competing physical targets prevent filename synchronization":
                e.status=="empty-reference"?"Filename field is empty; optional-field semantics have not been established (not a missing asset)":
                "Reference is external, crosses a link, or has an invalid path",detail,i);
        }
        for(const auto &value:collisionList) {
            const auto c=value.toObject();
            for(const auto &id:c["fileIds"].toArray())fail(id.toInt(),"physical-name-conflict",c["decision"].toString(),c["logicalKey"].toString());
        }
        const auto cycles=includeCycles();
        for(const auto &value:cycles) {
            const auto c=value.toObject();fail(c.value("sourceFileId").toInt(c["fileId"].toInt()),"include-cycle",c["issue"].toString());
        }
        QJsonArray entries,refs,failureList,warningList,missingList;
        for(const auto &w:missingTargets)missingList.append(QJsonObject{{"sourceFileId",w.fileId},{"path",files[w.fileId].path},
            {"reason","Target absent from scoped locations, or proven absent from the entire inventory; preserve the unresolved reference"},
            {"occurrences",w.occurrences},{"examples",strings(w.examples)},{"edgeIds",numbers(w.edgeIds)}});
        int readFailures=0,syncFailures=0,syntaxWarnings=0,warningOnly=0;QSet<int> failedFiles;
        for(const auto &f:failures) {
            failureList.append(QJsonObject{{"id",failureList.size()},{"category",f.category},{"code",f.code},{"sourceFileId",f.fileId},
                {"path",f.fileId<0?root:files[f.fileId].path},{"reason",f.reason},{"occurrences",f.occurrences},
                {"examples",strings(f.examples)},{"edgeIds",numbers(f.edgeIds)}});
            if(f.fileId>=0)failedFiles.insert(f.fileId);
            if(f.category=="file-read-failed")++readFailures;else ++syncFailures;
        }
        QCryptographicHash manifest(QCryptographicHash::Sha256),content(QCryptographicHash::Sha256);
        int dirs=0,parsed=0,invalid=0,diagnostics=0;
        for(int id=0;id<files.size();++id) {
            auto &e=files[id];
            if(e.directory)++dirs;else extensionCounts[QFileInfo(e.path).suffix().toLower()]++;
            coverageCounts[e.coverage]++;if(e.parsed)++parsed;if(e.parsed&&!e.valid)++invalid;
            diagnostics+=e.diagnostics.size();
            QJsonObject obj{{"id",id},{"path",e.path},{"directory",e.directory},{"link",e.link},
                {"size",e.size},{"modifiedMs",e.modified},{"coverage",e.coverage}};
            auto fingerprint=obj;fingerprint.remove("coverage");
            manifest.addData(QJsonDocument(fingerprint).toJson(QJsonDocument::Compact));
            if(!e.sha.isEmpty()) {obj["sha256"]=e.sha;content.addData((e.path+"\n"+e.sha+"\n").toUtf8());}
            if(e.parsed){obj["encoding"]=e.encoding;obj["compressed"]=e.compressed;obj["offsetUnit"]=e.offsets;
                obj["referenceScanComplete"]=e.document.referenceScanComplete;
                obj["referenceEditPolicy"]=frozen(id)?"rename-targets-only":"source-span patch subject to execution preflight";
                obj["syncAssessment"]=failedFiles.contains(id)?"failed":
                    (e.document.syntaxWarning||missingTargets.contains(id))?"warning-only":"no-known-failure-in-subset";
            }
            if(e.document.syntaxWarning) {
                ++syntaxWarnings;if(!failedFiles.contains(id))++warningOnly;
                warningList.append(QJsonObject{{"sourceFileId",id},{"path",e.path},
                    {"referenceScanComplete",e.document.referenceScanComplete},
                    {"syncAssessment",failedFiles.contains(id)?"also has filename-sync errors":"warning-only; references retained"},
                    {"diagnostics",strings(e.document.diagnostics)}});
            }
            if(!e.diagnostics.isEmpty())obj["diagnostics"]=strings(e.diagnostics);
            entries.append(obj);
            e.document=Document(); // Do not retain parser data alongside serialized plans.
        }
        for(int i=0;i<edges.size();++i) {
            const auto &e=edges[i];statusCounts[e.status]++;
            QJsonArray spans;for(const auto &s:e.literals)spans.append(QJsonObject{{"text",s.text},{"begin",s.begin},{"end",s.end}});
            refs.append(QJsonObject{{"id",i},{"sourceFileId",e.source},{"location",e.field},{"kind",e.kind},
                {"spelling",e.spelling},{"context",e.context},{"searchBases",strings(e.bases)},
                {"status",e.status},{"targetFileId",e.target},{"candidateFileIds",numbers(e.candidates)},
                {"selectedSearchBase",e.priority},{"implicit",e.implicit},{"optional",e.optional},
                {"sourceScalars",spans},{"scalarIndex",e.scalarIndex},{"fieldIndex",e.fieldIndex},
                {"sourceFamily",e.sourceFamily},{"suffix",e.suffix}});
            if(!e.authoredTexture.isEmpty()) {
                auto ref=refs.last().toObject();ref["authoredTextureReference"]=e.authoredTexture;
                ref["derivedDds"]=e.derivedDds;refs[refs.size()-1]=ref;
            }
        }
        auto counts=[](const QMap<QString,int>&map){QJsonObject o;for(auto it=map.cbegin();it!=map.cend();++it)o[it.key()]=it.value();return o;};
        return {{"schema","tsre-content-case-plan-v1"},{"stage","B3-plan"},{"gameRoot",root},
            {"applyReady",false},{"coverageCertified",false},
            {"coverageNote","Reference-subset inventory. Unknown fields/formats and implicit simulator rules remain review work. Execution isolates unresolved components and verifies preservation."},
            {"summary",QJsonObject{{"files",files.size()-dirs},{"directories",dirs},{"parsedDocuments",parsed},
                {"incompleteDocuments",invalid},{"documentDiagnostics",diagnostics},{"references",edges.size()},
                {"proposedOperations",ops.size()},{"collisionGroups",collisionList.size()},
                {"referenceConflictGroups",referenceConflicts.size()},
                {"textureNamingGroups",textureNamingGroups.size()},
                {"failedCases",failureList.size()},{"failedFiles",failedFiles.size()},
                {"fileReadFailures",readFailures},{"filenameSyncFailures",syncFailures},
                {"syntaxWarningDocuments",syntaxWarnings},{"syntaxWarningOnlyDocuments",warningOnly},
                {"missingTargetGroups",missingList.size()},{"missingTargetReferences",missingReferences},
                {"extensions",counts(extensionCounts)},{"coverage",counts(coverageCounts)},{"referenceStatuses",counts(statusCounts)}}},
            {"inventorySha256",QString::fromLatin1(manifest.result().toHex())},
            {"inspectedContentSha256",QString::fromLatin1(content.result().toHex())},
            {"diagnostics",strings(scanDiagnostics)},{"files",entries},{"references",refs},
            {"failures",failureList},{"syntaxWarnings",warningList},{"missingTargets",missingList},{"scopeWarnings",scopeWarnings},
            {"conflicts",collisionList},{"referenceConflicts",referenceConflicts},{"textureNamingGroups",textureNamingGroups},
            {"includeCycles",cycles},{"operations",ops}};
    }
};
QString cell(QString text) {return text.replace('|',"\\|").replace('\n'," ").replace('\r'," ");}
}
QJsonObject scan(const QString &root, QString &error, const std::function<void(const QString &)> &progress) {
    error.clear();const QFileInfo input(root);
    if(!input.exists()||!input.isDir()){error="Game root is not an existing directory: "+root;return {};}
    Scanner scanner;scanner.root=input.canonicalFilePath();scanner.progress=progress;
    if(progress)progress("Inventory: "+scanner.root);
    if(!scanner.inventory({})){error="Cannot inventory root";return {};}
    scanner.discoverRoutes();
    if(progress)progress(QString("Inventory complete: %1 entries; extracting references").arg(scanner.files.size()));
    scanner.extract();
    if(progress)progress("Building read-only plan");
    return scanner.finish();
}
QString markdownReport(const QJsonObject &plan) {
    const auto summary=plan["summary"].toObject();
    QString out="# Content filename case: dry-run report\n\nRoot: `"+plan["gameRoot"].toString()+"`\n\n";
    out+="No content was changed. This is a reference-subset scan, not a certified complete repair plan. Execution requires separate preflight and isolates unresolved components.\n\n";
    out+="## Errors\n\n";
    out+="Errors are grouped by source and reason. An isolated bad asset does not stop conversion of unaffected components; affected operations and shared dependencies must be isolated. Every group is listed here; JSON retains all affected edge IDs. Missing content in a known lookup location and recovered syntax warnings do not block conversion; they are listed separately below. Unknown lookup rules or unread filename fields remain errors.\n\n";
    out+="| ID | Category | Source | Reason and examples | Occurrences |\n| --- | --- | --- | --- | ---: |\n";
    for(const auto &value:plan["failures"].toArray()) {
        const auto f=value.toObject();QStringList examples;
        for(const auto &x:f["examples"].toArray())examples<<x.toString();
        const QString detail=f["code"].toString()+": "+f["reason"].toString()+". "+examples.join("; ");
        out+="| "+QString::number(f["id"].toInt())+" | "+QString(f["category"].toString()).replace("-failed","-error")+" | "+cell(f["path"].toString())+
            " | "+cell(detail)+" | "+QString::number(f["occurrences"].toInt())+" |\n";
    }
    if(plan["failures"].toArray().isEmpty())out+="\nNo file-read or filename-sync errors detected in the supported subset.\n";
    out+="\n## Scope exclusions (warnings)\n\nExcluded sources are not parsed or proposed for renaming. Their reference coverage is outside this plan.\n\n| Path | Reason |\n| --- | --- |\n";
    for(const auto &value:plan["scopeWarnings"].toArray()) {
        const auto w=value.toObject();out+="| "+cell(w["path"].toString())+" | "+cell(w["reason"].toString())+" |\n";
    }
    out+="\n## Missing content (warnings)\n\nLookup is scoped to each reference's expected locations. A same-named file elsewhere in the game root is not a substitute. Leave unresolved references intact; these warnings alone do not fail the scan.\n\n";
    out+="| Source | Missing reference examples and search locations | Occurrences |\n| --- | --- | ---: |\n";
    for(const auto &value:plan["missingTargets"].toArray()) {
        const auto w=value.toObject();QStringList examples;for(const auto &x:w["examples"].toArray())examples<<x.toString();
        out+="| "+cell(w["path"].toString())+" | "+cell(examples.join("; "))+" | "+QString::number(w["occurrences"].toInt())+" |\n";
    }
    out+="\n## Syntax warnings and reference recovery\n\nSource strings are frozen after syntax recovery. Targets must be named to match; those sources are not proposed for rewriting. Unknown lookup rules, unread filename fields, and naming conflicts remain errors even when unrelated syntax recovery succeeded. Missing content remains a warning.\n\n";
    out+="| Source | Reference scan complete | Sync assessment | Diagnostic |\n| --- | --- | --- | --- |\n";
    for(const auto &value:plan["syntaxWarnings"].toArray()) {
        const auto w=value.toObject();QStringList diagnostics;for(const auto &d:w["diagnostics"].toArray())diagnostics<<d.toString();
        out+="| "+cell(w["path"].toString())+" | "+(w["referenceScanComplete"].toBool()?"yes":"no")+" | "+
            cell(w["syncAssessment"].toString())+" | "+cell(diagnostics.join("; "))+" |\n";
    }
    out+="\n## Summary\n\n";
    out+="| Measure | Count |\n| --- | ---: |\n";
    for(const auto &key:{"files","directories","parsedDocuments","failedCases","fileReadFailures","filenameSyncFailures","missingTargetGroups","missingTargetReferences","syntaxWarningDocuments","syntaxWarningOnlyDocuments","documentDiagnostics","references","proposedOperations","collisionGroups","referenceConflictGroups","textureNamingGroups"})
        out+="| "+QString(key)+" | "+QString::number(summary[key].toInt())+" |\n";
    for(const auto &group:{"referenceStatuses","coverage","extensions"}) {
        out+="\n## "+QString(group)+"\n\n| Category | Count |\n| --- | ---: |\n";
        const auto counts=summary[group].toObject();for(auto it=counts.begin();it!=counts.end();++it)out+="| "+cell(it.key())+" | "+QString::number(it.value().toInt())+" |\n";
    }
    out+="\nInventory SHA-256: `"+plan["inventorySha256"].toString()+"`\n\nInspected content SHA-256: `"+plan["inspectedContentSha256"].toString()+"`\n";
    const auto files=plan["files"].toArray();
    out+="\nReference counts include repeated route/include contexts and optional definitions. Missing candidates are not a count of broken playable routes. All operations are provisional; leaf resources are generally inventoried by metadata, not hashed.\n";
    out+="\n## Inventory diagnostics\n\n";
    for(const auto &d:plan["diagnostics"].toArray())out+="- "+cell(d.toString())+"\n";
    out+="\n## Texture naming coordination (first 100)\n\nSeparate physical assets remain separate. Shared fields, existing ACE/DDS representations and seasonal variants are named jointly. Full groups and file IDs are in JSON.\n\n";
    int groupsShown=0;
    for(const auto &value:plan["textureNamingGroups"].toArray()) {
        if(groupsShown++>=100)break;const auto group=value.toObject();
        out+="- Group "+QString::number(group["id"].toInt())+": `"+cell(group["logicalFilename"].toString())+
            "`, "+QString::number(group["fileIds"].toArray().size())+" files; "+cell(group["decision"].toString())+".\n";
    }
    out+="\n## Conflicting shared-field proposals (first 100)\n\n";
    int conflictsShown=0;
    for(const auto &value:plan["referenceConflicts"].toArray()) {
        if(conflictsShown++>=100)break;const auto c=value.toObject();QStringList choices;
        for(const auto &a:c["alternatives"].toArray())choices<<a.toObject()["spelling"].toString();
        out+="- `"+cell(files[c["sourceFileId"].toInt()].toObject()["path"].toString())+"` ("+cell(c["location"].toString())+"): "+cell(choices.join(" / "))+"; blocked pending coordinated naming.\n";
    }
    int count=0;
    out+="\n## Physical filename conflicts (first 100)\n\n";count=0;
    for(const auto &value:plan["conflicts"].toArray()) {
        if(count++>=100)break;const auto conflict=value.toObject();
        out+="- `"+cell(conflict["logicalKey"].toString())+"`: "+conflict["classification"].toString()+"\n";
    }
    out+="\n## Include cycles (first 100)\n\n";count=0;
    for(const auto &value:plan["includeCycles"].toArray()) {
        if(count++>=100)break;const auto cycle=value.toObject();
        out+="- "+cycle["issue"].toString()+": `"+cell(files[cycle.value("sourceFileId").toInt(cycle["fileId"].toInt())].toObject()["path"].toString())+"`\n";
    }
    out+="\n## Coverage and parser diagnostics (first 100)\n\n";
    count=0;
    for(const auto &value:files) {
        const auto f=value.toObject();for(const auto &d:f["diagnostics"].toArray()) {
            if(count++<100)out+="- `"+cell(f["path"].toString())+"`: "+cell(d.toString())+"\n";
        }
    }
    out+="\n## Reference issues (first 100)\n\n| Source | Field | Reference | Status |\n| --- | --- | --- | --- |\n";
    count=0;for(const auto &value:plan["references"].toArray()) {
        const auto e=value.toObject();const auto status=e["status"].toString();
        if(status=="exact"||status=="optional-missing")continue;if(count++>=100)break;
        const auto source=files[e["sourceFileId"].toInt()].toObject()["path"].toString();
        out+="| "+cell(source)+" | "+cell(e["location"].toString())+" | "+cell(e["spelling"].toString())+" | "+status+" |\n";
    }
    out+="\n## Proposed operations (first 100)\n\nAll proposals require coverage/collision review; reference writers are not implemented.\n\n";
    count=0;for(const auto &value:plan["operations"].toArray()) {
        if(count++>=100)break;const auto op=value.toObject();
        out+="- "+op["operation"].toString()+": `"+cell(op.value("from").toString(op["originalLogicalReference"].toString()))+
            "` -> `"+cell(op.value("to").toString(op["proposedLogicalReference"].toString()))+"`\n";
    }
    out+="\nThe JSON plan contains the full inventory, edges, scalar offsets, conflicts, include cycles and proposals.\n";
    return out;
}
}
