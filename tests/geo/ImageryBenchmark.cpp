// Explicit opt-in live imagery probe. It is not part of ctest or a normal build.
#include <tsre/geo/ImagerySource.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <cmath>
#include <iostream>

int main(int argc,char **argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()!=8||args[1]!="--live"){
        std::cerr<<"Usage: tsre_imagery_benchmark --live cache-root dataset-id latitude longitude source-pixels output.png\n";
        return 2;
    }
    bool latOk=false,lonOk=false,sizeOk=false;
    const double latitude=args[4].toDouble(&latOk),longitude=args[5].toDouble(&lonOk);
    const int sourcePixels=args[6].toInt(&sizeOk);
    if(!latOk||!lonOk||!sizeOk||sourcePixels<256||sourcePixels>8192)return 2;
    constexpr double terrainSize=2048,half=terrainSize/2;
    const double dy=half/111320.0;
    const double dx=half/(111320.0*std::cos(latitude*3.14159265358979323846/180.0));
    Imagery::Request request;request.root=args[2];request.datasetId=args[3];
    request.width=request.height=512;request.sourcePixels=sourcePixels;
    request.controlColumns=request.controlRows=2;request.terrainSizeMetres=terrainSize;
    request.controlPoints={{latitude+dy,longitude-dx},{latitude+dy,longitude+dx},
                           {latitude-dy,longitude-dx},{latitude-dy,longitude+dx}};
    std::atomic_bool cancel{false};QElapsedTimer timer;timer.start();
    const auto result=Imagery::generate(request,cancel);
    std::cout<<"success="<<result.success()<<" tiles="<<result.report.tiles
             <<" downloads="<<result.report.downloads<<" cache="<<result.report.cacheHits
             <<" bytes="<<result.report.downloadedBytes<<" sourceMpp="
             <<result.report.sourceMetresPerPixel<<" elapsedMs="<<timer.elapsed()<<'\n';
    if(!result.error.isEmpty())std::cerr<<result.error.toStdString()<<'\n';
    if(!result.report.issues.isEmpty())std::cerr<<result.report.issues.join('\n').toStdString()<<'\n';
    if(!result.success()||!result.image.save(args[7],"PNG"))return 1;
    return 0;
}
