#include "ContentCase.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>

namespace ContentCase {
namespace {
bool inside(QString root,QString path) {
    root=QDir::cleanPath(root).replace('\\','/');path=QDir::cleanPath(path).replace('\\','/');
    return path.compare(root,Qt::CaseInsensitive)==0 || path.startsWith(root+"/",Qt::CaseInsensitive);
}
bool outputPath(const QString &root,const QString &input,QString &path,QString &error) {
    if(input.isEmpty())return true;
    const QFileInfo info(input);
    if(info.exists()||info.isSymLink()||info.isJunction()) {error="Output already exists (never overwritten): "+input;return false;}
    const QFileInfo directory(info.absolutePath());
    const QString canonical=directory.canonicalFilePath();
    if(canonical.isEmpty()||!directory.isDir()) {error="Output parent directory must already exist: "+input;return false;}
    path=QDir(canonical).filePath(info.fileName());
    if(inside(root,path)) {error="Output must be outside the game root: "+input;return false;}
    return true;
}
bool writeNew(const QString &path,const QByteArray &bytes,QString &error) {
    if(path.isEmpty())return true;
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) {error="Cannot create new output: "+path+": "+file.errorString();return false;}
    if(file.write(bytes)!=bytes.size()||!file.flush()) {error="Output write failed: "+path;return false;}
    return true;
}
}
int run(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTextStream out(stdout),err(stderr);
    auto fail=[&](const QString &message){err<<"Content case: "<<message<<"\n";return 2;};
    QString root,planInput,reportInput;bool plan=false,help=false,mode=false;
    const auto args=app.arguments();
    for(int i=1;i<args.size();++i) {
        const auto arg=args[i];
        if(arg=="--contentcase"){mode=true;continue;}
        if(arg=="--help"||arg=="-h"){help=true;continue;}
        if(arg=="--apply"||arg=="--rollback"||arg=="--verify")
            return fail(arg+" is not implemented in the dry-run stage; use --plan");
        if(arg=="--plan") {
            if(plan)return fail("Duplicate --plan");plan=true;
            // The optional output follows --plan; put the root before --plan.
            if(!root.isEmpty() && i+1<args.size() && !args[i+1].startsWith('-'))planInput=args[++i];
            continue;
        }
        if(arg=="--report") {
            if(!reportInput.isEmpty()||i+1>=args.size()||args[i+1].startsWith('-'))return fail("--report requires one new output path");
            reportInput=args[++i];continue;
        }
        if(arg=="--") {
            if(!root.isEmpty()||i+2!=args.size())return fail("Supply one game root");root=args[++i];continue;
        }
        if(arg.startsWith('-'))return fail("Unknown option: "+arg);
        if(!root.isEmpty())return fail("Supply one game root; optional plan output belongs after --plan");
        root=arg;
    }
    if(help) {
        out<<"Usage: TSRE5vc --contentcase <gameroot> --plan [new-plan.json] [--report new-report.md]\n"
             "Stage 1 is read-only. No content is renamed, saved or overwritten.\n"
             "Outputs are optional, must not exist, and must be outside gameroot.\n"
             "Exit 0: no known read/sync errors in the reference subset; 1: scan completed with errors; 2: usage/output error.\n"
             "Recovered syntax warnings alone do not cause exit 1.\n";
        return 0;
    }
    if(!mode||!plan)return fail("Repair execution is not implemented in the dry-run stage; use --contentcase <gameroot> --plan");
    const QFileInfo rootInfo(root);
    if(root.isEmpty()||!rootInfo.isDir())return fail("Supply an existing game-root directory");
    const QString canonicalRoot=rootInfo.canonicalFilePath();
    QString planPath,reportPath,error;
    if(!outputPath(canonicalRoot,planInput,planPath,error)||!outputPath(canonicalRoot,reportInput,reportPath,error))return fail(error);
    if(!planPath.isEmpty()&&planPath.compare(reportPath,Qt::CaseInsensitive)==0)return fail("Plan and report must have different paths");
    const auto result=scan(canonicalRoot,error,[&](const QString &message){err<<message<<"\n";err.flush();});
    if(result.isEmpty())return fail(error);
    // Recheck output parents after the potentially long scan. No input file is opened writable.
    QString checked;
    if(!outputPath(canonicalRoot,planPath,checked,error)||!outputPath(canonicalRoot,reportPath,checked,error))return fail(error);
    if((!planPath.isEmpty() && !writeNew(planPath,QJsonDocument(result).toJson(QJsonDocument::Compact),error)) ||
       (!reportPath.isEmpty() && !writeNew(reportPath,markdownReport(result).toUtf8(),error)))return fail(error);
    QJsonObject summary{{"gameRoot",canonicalRoot},{"summary",result["summary"]},
        {"inventorySha256",result["inventorySha256"]},{"inspectedContentSha256",result["inspectedContentSha256"]},
        {"coverageCertified",false},{"applyReady",false}};
    out<<QJsonDocument(summary).toJson(QJsonDocument::Indented);
    return result["summary"].toObject()["failedCases"].toInt()>0 ? 1 : 0;
}
}
