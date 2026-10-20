#include "ForestObjectsTestSuite.h"
#include "TokenTestSupport.h"
#include <tsre/world/objects/ForestObj.h>
#include <QScopedValueRollback>

int TsreTests::runForestObjectsSuite(bool verbose) {
    TokenTest::Suite test{"[tests:forest-objects]", verbose};

    ForestObj forest;
    auto shapes = [&](const QString &name) {
        forest.treeTexture = name;
        return forest.shapeTrees();
    };
    test.check(shapes("tree.s") && shapes("tree.gltf") && shapes("tree.glb") && shapes("Tree.GLB"),
               "shapes: .s, .gltf and .glb in any case");
    test.check(!shapes("tree.ace") && !shapes("tree.dds") && !shapes("") && !shapes("trees"),
               "textures and names without one: 2D trees");

    // Two definitions of the same texture and one of a shape.
    QScopedValueRollback<QVector<ForestObj::ForestList>> list(ForestObj::forestList);
    ForestObj::forestList.clear();
    ForestObj::forestList.push_back({"Small", "tree.ace", 0.9f, 1.1f, 8.0f, 10.0f});
    ForestObj::forestList.push_back({"Large", "tree.ace", 0.9f, 1.1f, 16.0f, 20.0f});
    ForestObj::forestList.push_back({"Spruce", "spruce.glb", 0.8f, 1.2f, 16.0f, 20.0f});

    forest.treeTexture = "TREE.ace";
    forest.treeSizeX = 16.0f;
    forest.treeSizeZ = 20.0f;
    forest.scaleRangeX = 0.9f;
    forest.scaleRangeZ = 1.1f;
    test.check(forest.definitionId() == 1, "definition: its texture and sizes");
    forest.treeSizeX = 4.0f;
    test.check(forest.definitionId() == 0, "definition: its texture with other sizes, the first");
    forest.treeTexture = "other.ace";
    test.check(forest.definitionId() == -1, "definition: none for another texture");

    forest.setDefinition(2);
    test.check(forest.treeTexture == "spruce.glb" && forest.treeSizeX == 16.0f && forest.treeSizeZ == 20.0f
                   && forest.scaleRangeX == 0.8f && forest.scaleRangeZ == 1.2f && forest.shapeTrees()
                   && forest.definitionId() == 2,
               "a definition taken: its shape, sizes and scale range");
    forest.setDefinition(7);
    test.check(forest.treeTexture == "spruce.glb", "an unknown definition changes nothing");
    return test.finish();
}
