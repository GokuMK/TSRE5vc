#include <tsre/tests/ObjSearchTestSuite.h>

#include <routeEditor/ObjTools.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/world/Ref.h>

namespace {
Ref::RefItem model(const QString &description, const QStringList &paths) {
    Ref::RefItem item;
    item.description = description;
    for (const QString &path : paths)
        item.filename.push_back(path);
    return item;
}
}

// The palette search finds an entry by the name of its model file (.s), not
// only by the description the list shows, and ranks the file name matches
// above the description matches.
int TsreTests::runObjSearchSuite(bool verbose) {
    TokenTest::Suite test{"[tests:obj-search]", verbose};
    const Ref::RefItem gp38 = model("Diesel locomotive", {"C:/TSRE/models/GP38.s"});

    test.check(ObjTools::refItemSearchRank(gp38, "GP38.s") == 0, "exact model file name with extension");
    test.check(ObjTools::refItemSearchRank(gp38, "gp38") == 0, "model file name without extension, any case");
    test.check(ObjTools::refItemSearchRank(gp38, "  Gp38.S  ") == 0, "surrounding spaces and case ignored");
    test.check(ObjTools::refItemSearchRank(gp38, "models/GP38.s") == 0, "relative path with extension");
    test.check(ObjTools::refItemSearchRank(gp38, "GP3") == 1, "partial model file name");
    test.check(ObjTools::refItemSearchRank(gp38, "diesel") == 2, "description only");
    test.check(ObjTools::refItemSearchRank(gp38, "boxcar") == -1, "no match");

    const Ref::RefItem windowsPath = model("", {"C:\\TSRE\\models\\SW1500.s"});
    test.check(ObjTools::refItemSearchRank(windowsPath, "SW1500.s") == 0, "backslash path in the entry");
    test.check(ObjTools::refItemSearchRank(windowsPath, "models\\sw1500") == 0, "backslash path in the text");
    test.check(ObjTools::refItemSearchRank(windowsPath, "C:/TSRE/models/SW1500.s") == 0, "full path in the text");

    const Ref::RefItem numbered = model("Boxcar", {"a/40ft.s", "b/50ft.s"});
    test.check(ObjTools::refItemSearchRank(numbered, "50ft") == 0, "second model file name matches");

    const Ref::RefItem descriptionOnly = model("Caboose", {});
    test.check(ObjTools::refItemSearchRank(descriptionOnly, "caboose") == 2, "description without model files");
    test.check(ObjTools::refItemSearchRank(descriptionOnly, "caboose.s") == -1, "no model file to match");

    const Ref::RefItem both = model("GP38 diesel", {"GP38.s"});
    test.check(ObjTools::refItemSearchRank(both, "GP") == 1, "partial file name outranks the description");
    test.check(ObjTools::refItemSearchRank(both, "GP38.s") == 0, "file name still ranks first");
    test.check(ObjTools::refItemSearchRank(both, "") == 2, "empty text lists everything");
    return test.finish();
}
