#ifndef TERRAINNORMALTESTSUITE_H
#define TERRAINNORMALTESTSUITE_H
namespace TsreTests {
struct TestRunOptions;
int runTerrainNormalSuite(bool verbose);
int runTerrainNormalBenchmark(const TestRunOptions &opts);
}
#endif
