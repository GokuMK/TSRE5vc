#include <tsre/tests/EditorToolsTestSuite.h>

#include <QAction>
#include <QDebug>
#include <QKeyEvent>
#include <QMenu>
#include <QPushButton>
#include <QWidget>
#include <routeEditor/tools/ToolButtons.h>
#include <routeEditor/tools/ToolContext.h>
#include <routeEditor/tools/ToolRegistry.h>
#include <tsre/texture/Brush.h>

namespace {

// Records what tools ask of the editor; no route is loaded.
class FakeContext : public ToolContext {
public:
    QWidget widget;
    ViewMode mode = ViewMode::Scene3D;
    float pointerPosition[3] = {1, 2, 3};
    float rotation[4] = {0, 0, 0, 1};
    ObjectEdit edit = ObjectEdit::Select;
    bool selectionRequested = false;
    bool continuousFlexPlaced = false;
    bool rulerPlaced = false;
    bool flexDataSent = false;
    bool terrainToSelectedCalled = false;
    bool placeResult = true;
    QString activated;

    QWidget *view() override { return &widget; }
    ViewMode viewMode() const override { return mode; }
    Route *currentRoute() const override { return nullptr; }
    int tileX() const override { return 10; }
    int tileZ() const override { return -20; }
    float *pointer() override { return pointerPosition; }
    float cameraHeading() const override { return 0.0f; }
    Brush paintBrush;
    Brush *brush() override { return &paintBrush; }
    GameObj *selected() const override { return nullptr; }
    void select(GameObj *) override {}
    void setLastSelected(GameObj *) override {}
    void requestSelectionPass() override { selectionRequested = true; }
    bool pointerOnTrack(int &, int &, float *) override { return false; }
    ObjectEdit objectEdit() const override { return edit; }
    void setObjectEdit(ObjectEdit chosen) override { edit = chosen; }
    bool sticks = true;
    bool pointerSticksToTerrain() const override { return sticks; }
    void setPointerSticksToTerrain(bool terrainOnly) override { sticks = terrainOnly; }
    bool shiftDown() const override { return false; }
    bool control = false;
    bool controlDown() const override { return control; }
    float keyMoveStep() const override { return 0.25f; }
    bool stepReset = false;
    void resetKeyMoveStep() override { stepReset = true; }
    float *placementRotation() override { return rotation; }
    float placementElevation() const override { return 0.0f; }
    bool autoAddToTrackDb() const override { return true; }
    bool rotationReset = false;
    void resetPlacementRotation() override { rotationReset = true; }
    void rememberPlacement() override {}
    void terrainToSelected() override { terrainToSelectedCalled = true; }
    void selectedPositionToTerrain() override {}
    void selectedRotationToTerrain() override {}
    void pickPlacementFromSelected() override {}
    void pickPlacementRotation() override {}
    void pickPlacementRotationAndElevation() override {}
    bool placeContinuousFlex(float *) override { continuousFlexPlaced = true; return placeResult; }
    bool placeContinuousRulerPoint(const float *) override { rulerPlaced = true; return placeResult; }
    void startTelepole(TelepoleObj *) override {}
    void activateTool(const QString &id) override { activated = id; }
    void message(const QString &) override {}
    QString lastMessage;
    void message(const QString &name, const QString &value) override { lastMessage = name + value; }
    void sendFlexData() override { flexDataSent = true; }
    void reportTextureId(int) override {}
    void reportMaterialPicked() override {}
    void openMapTileWindow(Terrain *) override {}
    void openImageryWindow(Terrain *) override {}
};

}

int TsreTests::runEditorToolsSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const char *name) {
        if (condition) {
            ++passed;
            if (verbose)
                qInfo() << "[tests:editor-tools] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:editor-tools] FAIL" << name;
        }
    };

    ToolRegistry registry;
    const QStringList objectTools = {"selectTool", "placeTool", "autoPlaceSimpleTool",
                                     "signalLinkTool", "FlexTool", "continuousFlexTool",
                                     "continuousFlexRoadTool", "continuousRulerTool"};
    bool allFound = true;
    for (const QString &id : objectTools)
        allFound = allFound && registry.find(id) != nullptr && registry.find(id)->id() == id;
    check(allFound, "the object tools are registered by their panel names");
    check(registry.find("proceduralPickTool") == nullptr
          && registry.allowed("proceduralPickTool", ViewMode::Scene3D)
          && !registry.allowed("proceduralPickTool", ViewMode::Map),
          "a name without a tool stays valid in 3D and is 3D only");
    check(registry.allowed("", ViewMode::Map) && registry.allowed("", ViewMode::Scene3D),
          "no tool is allowed in every mode");
    check(registry.allowed("placeTool", ViewMode::Scene3D)
          && !registry.allowed("placeTool", ViewMode::Map)
          && registry.allowed("selectTool", ViewMode::Map),
          "placing is 3D only; selecting works on the map too");

    FakeContext ctx;
    EditorTool *select = registry.find("selectTool");
    const ToolMouse mouse{QPointF(5, 5), QPointF(5, 5)};
    check(select->press(ctx, mouse) && ctx.selectionRequested,
          "a select click asks for a selection pass");
    ctx.selectionRequested = false;
    ctx.edit = ToolContext::ObjectEdit::Rotate;
    select->press(ctx, mouse);
    check(!ctx.selectionRequested, "select in rotate mode keeps the selection");
    ctx.edit = ToolContext::ObjectEdit::Select;

    EditorTool *flex = registry.find("continuousFlexTool");
    check(!flex->press(ctx, mouse) && ctx.continuousFlexPlaced,
          "continuous flex places a point and ends the press");
    EditorTool *ruler = registry.find("continuousRulerTool");
    check(!ruler->press(ctx, mouse) && ctx.rulerPlaced,
          "continuous ruler places a point and ends the press");
    check(registry.find("FlexTool")->press(ctx, mouse) && ctx.flexDataSent,
          "the flex point tool sends the pointer to the flex properties");

    QKeyEvent keyF(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier);
    QKeyEvent keyA(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    check(select->key(ctx, &keyF) && ctx.terrainToSelectedCalled,
          "F under select fits the terrain to the selected object");
    check(!select->key(ctx, &keyA), "keys the tool does not use pass on");
    check(select->wheel(ctx, 1.2f), "select and place use the wheel");
    check(!registry.find("autoPlaceSimpleTool")->wheel(ctx, 1.2f),
          "auto place leaves the wheel alone");

    QMenu menu;
    ctx.edit = ToolContext::ObjectEdit::Translate;
    select->contextMenu(ctx, menu);
    QMenu *mode = menu.actions().isEmpty() ? nullptr : menu.actions().first()->menu();
    int checkedIndex = -1;
    if (mode != nullptr)
        for (int i = 0; i < mode->actions().size(); ++i)
            if (mode->actions()[i]->isChecked())
                checkedIndex = i;
    check(mode != nullptr && mode->actions().size() == 4 && checkedIndex == 2,
          "the select menu offers four modes with the current one checked");
    if (mode != nullptr && mode->actions().size() == 4)
        mode->actions()[1]->trigger();
    check(ctx.edit == ToolContext::ObjectEdit::Rotate, "a mode entry sets the select mode");
    QMenu placeMenu;
    registry.find("placeTool")->contextMenu(ctx, placeMenu);
    check(placeMenu.actions().size() == 3, "place offers pointer, move step and rotation reset");
    if (placeMenu.actions().size() == 3) {
        placeMenu.actions()[0]->menu()->actions()[1]->trigger();
        placeMenu.actions()[1]->trigger();
        placeMenu.actions()[2]->trigger();
    }
    check(!ctx.sticks && ctx.stepReset && ctx.rotationReset,
          "the place menu sets the pointer and resets step and rotation");

    // Terrain tools.
    const QStringList terrainTools = {"heightTool", "waterTerrTool", "gapsTerrainTool",
                                      "paintToolColor", "paintToolTexture",
                                      "proceduralPaintTextureTool", "proceduralFillPatchTool",
                                      "proceduralFillTool", "pickTerrainTexTool",
                                      "proceduralTileEnableTool", "proceduralTileDisableTool",
                                      "putTerrainTexTool", "drawTerrTool", "waterHeightTileTool",
                                      "fixedTileTool", "lockTexTool", "makeTileTextureTool",
                                      "removeTileTextureTool"};
    allFound = true;
    for (const QString &id : terrainTools)
        allFound = allFound && registry.find(id) != nullptr;
    check(allFound, "the terrain tools are registered by their panel names");
    EditorTool *height = registry.find("heightTool");
    QKeyEvent keyZ(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier);
    ctx.paintBrush.direction = 1;
    check(height->key(ctx, &keyZ) && ctx.paintBrush.direction == -1 && ctx.lastMessage == "brushDirection-",
          "Z flips the height brush and tells the panel");
    ctx.control = true;
    check(!height->key(ctx, &keyZ) && ctx.paintBrush.direction == -1,
          "Control Z is left to undo");
    ctx.control = false;
    QMenu directionMenu;
    registry.find("gapsTerrainTool")->contextMenu(ctx, directionMenu);
    QMenu *direction = directionMenu.actions().isEmpty() ? nullptr : directionMenu.actions().first()->menu();
    if (direction != nullptr && direction->actions().size() == 2)
        direction->actions()[0]->trigger();
    check(direction != nullptr && ctx.paintBrush.direction == 1 && ctx.lastMessage == "brushDirection+",
          "the direction menu sets the brush direction");
    QMenu orientationMenu;
    registry.find("putTerrainTexTool")->contextMenu(ctx, orientationMenu);
    QMenu *orientation = orientationMenu.actions().isEmpty() ? nullptr
                                                             : orientationMenu.actions().first()->menu();
    if (orientation != nullptr && orientation->actions().size() == 6)
        orientation->actions()[3]->trigger();
    check(orientation != nullptr && ctx.paintBrush.texTransformation == Brush::ROT90,
          "the texture menu sets the patch orientation");
    QMenu paintMenu;
    registry.find("paintToolTexture")->contextMenu(ctx, paintMenu);
    check(!paintMenu.actions().isEmpty() && paintMenu.actions().first()->menu() != nullptr
          && paintMenu.actions().first()->menu()->actions().size() == 4,
          "painting offers four automatic paints");

    // Geo and activity tools; every name the panels send.
    const QStringList dataTools = {"mapTileShowTool", "mapTileLoadTool", "imageryTileLoadTool",
                                   "heightTileLoadTool", "actNewLooseConsistTool",
                                   "actNewSpeedZoneTool", "pickNewEventLocationTool"};
    allFound = true;
    for (const QString &id : dataTools)
        allFound = allFound && registry.find(id) != nullptr;
    check(allFound, "the geo and activity tools are registered by their panel names");
    // Names the panels send that need no mouse handling (live flex is a
    // view state; the others act in their panels).
    const QStringList withoutTool = {"liveFlexTool", "proceduralPickTool", "proceduralLockTool",
                                     "setTexTool", "waTileTool"};
    bool noneRegistered = true;
    for (const QString &id : withoutTool)
        noneRegistered = noneRegistered && registry.find(id) == nullptr;
    check(noneRegistered && registry.ids().size() == objectTools.size() + terrainTools.size()
                                                     + dataTools.size(),
          "every other panel name has a tool, and no tool is registered twice");

    // Tool panel buttons follow the view mode; a button's own condition
    // stays with it.
    QPushButton placeButton, picker, needsSelection;
    const QMap<QString, QPushButton *> buttons = {
        {"placeTool", &placeButton}, {"proceduralPickTool", &picker},
        {"autoPlaceSimpleTool", &needsSelection}, {"", nullptr}};
    ToolButtons::setRegistry(&registry);
    ToolButtons::setAvailable(&needsSelection, false);
    ToolButtons::applyMode(buttons, ToolButtons::modeOf("map"));
    const bool mapOff = !placeButton.isEnabled() && !picker.isEnabled()
            && !needsSelection.isEnabled();
    ToolButtons::setAvailable(&needsSelection, true);
    const bool stillOff = !needsSelection.isEnabled();
    ToolButtons::applyMode(buttons, ToolButtons::modeOf("3d"));
    check(mapOff && stillOff && placeButton.isEnabled() && picker.isEnabled()
                  && needsSelection.isEnabled(),
          "panel buttons: off in map mode for 3D tools, back in 3D with their own condition");
    ToolButtons::setAvailable(&needsSelection, false);
    check(!needsSelection.isEnabled() && placeButton.isEnabled(),
          "panel buttons: a button's own condition still disables it in 3D");
    check(registry.allowed("actNewLooseConsistTool", ViewMode::Map)
                  && registry.allowed("actNewSpeedZoneTool", ViewMode::Map)
                  && registry.allowed("pickNewEventLocationTool", ViewMode::Map)
                  && ToolButtons::allowed("pickNewEventLocationTool", ViewMode::Map)
                  && !ToolButtons::allowed("placeTool", ViewMode::Map),
          "activity tools work in map mode; the others stay 3D only");
    ToolButtons::setRegistry(nullptr);

    qInfo().noquote() << "[tests:editor-tools] cases=" << passed + failed << "passed=" << passed
                      << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
