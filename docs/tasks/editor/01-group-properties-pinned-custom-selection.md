# Task 01 - Group Properties Child List, Pinned Main Group, And Custom Group Selection

## Objective
Add child-object browsing to `PropertiesGroup`, support custom subgroup selection from that panel, and keep the properties panel pinned to the original main group until the user exits that mode.

This task note captures the current behavior, the agreed design direction, and the places where existing `groupObj` assumptions may require updates.

## Requested UX
- When a `GroupObj` is selected, show a list of child objects in the properties panel.
- Allow actions on a selected child:
  - `Select`
  - `Select Similar`
- `Select Similar` must follow the same rule as current `Select Similar`.
- During custom subgroup selection, keep the properties panel on the original main group.
- Add `Reselect This Group Object` while the panel is pinned.
- For now, any unrelated selection or delete may destroy/unpin the pinned group state and close or switch the properties panel normally.

## Current Similarity Rule
Current `Select Similar` does not use a generic shape-path comparison. It scans loaded tiles and calls `WorldObj::isSimilar(...)`.

Current implementations found:
- `StaticObj::isSimilar(...)`: same `typeID` and same `fileName`
- `TrackObj::isSimilar(...)`: same `typeID` and same `fileName`
- `SignalObj::isSimilar(...)`: same `typeID` and same `fileName`
- base `WorldObj::isSimilar(...)`: always `false`

Relevant code:
- `src/routeEditor/RouteEditorGLWidget.cpp:1976`
- `src/tsre/world/Route.cpp:1849`
- `src/tsre/world/Tile.cpp:814`
- `src/tsre/world/objects/StaticObj.cpp:489`
- `src/tsre/world/objects/TrackObj.cpp:283`
- `src/tsre/world/objects/SignalObj.cpp:672`
- `src/tsre/world/objects/WorldObj.cpp:586`

Implementation consequence:
- `Select Similar` inside the current group should reuse `isSimilar(...)` on the chosen child for consistency.

## Current Selection / Properties Flow
Selection and properties are currently tightly coupled.

- `RouteEditorGLWidget::setSelectedObj(...)` updates `selectedObj`, updates `Game::currentSelectedGameObj`, and emits `showProperties(selectedObj)`.
- `RouteEditorWindow::showProperties(...)` hides all property widgets and shows the one supporting the currently selected object.
- `RouteEditorWindow::updateProperties(...)` updates only the currently visible property widget.
- `RouteEditorGLWidget` periodically emits `updateProperties(selectedObj)`.

Relevant code:
- `src/routeEditor/RouteEditorGLWidget.cpp:1810`
- `src/routeEditor/RouteEditorGLWidget.cpp:123`
- `src/routeEditor/RouteEditorWindow.cpp:765`
- `src/routeEditor/RouteEditorWindow.cpp:786`

Implementation consequence:
- Pinned group properties require changes in both `showProperties(...)` and `updateProperties(...)`.

## Agreed Design Direction
Use a pinned object on the properties side.

Suggested behavior:
- Properties window stores `PinnedObject`, usually `NULL`.
- When user triggers custom subgroup selection from `PropertiesGroup`:
  - pin the original main group object
  - create or pass a custom subgroup selection into the normal editor selection flow
- GL widget selection should work normally.
- When properties receive `showProperties(...)` / `updateProperties(...)`:
  - if `PinnedObject == NULL`, behave as today
  - if `PinnedObject != NULL` and the request is for the custom subgroup created from the pinned panel, ignore it and keep showing the pinned main group
  - if the request is for a different object, clear `PinnedObject` and switch normally
- `Reselect This Group Object` selects the pinned group normally and clears the pin.

## Custom Group Selection: Preferred Model
Preferred model: pass a custom `GroupObj` as a normal selected object through `objectSelected(GameObj* obj)`.

Reason:
- It allows the original main group and the custom subgroup to exist as two separate group objects.
- Reusing `objectSelected(QVector<GameObj*>)` would rebuild the shared widget `groupObj`, which conflicts with the need to keep the original main group pinned in properties.

Important note:
- This is only safe if code paths that assume "selected group == RouteEditorGLWidget::groupObj" are reviewed and updated.

## Analysis Of `setSelectedObj(NULL)` In `objectSelected(...)`
### `objectSelected(GameObj* obj)`
Current code clears selection first, then selects the new object.

Observation:
- The normal mouse world-object selection path already switches directly from old object to new object without an intermediate `setSelectedObj(NULL)`.
- Moving `setSelectedObj(NULL)` into the `if(obj == NULL)` branch appears low-risk and would avoid a transient properties clear.

Relevant code:
- `src/routeEditor/RouteEditorGLWidget.cpp:1780`
- `src/routeEditor/RouteEditorGLWidget.cpp:913`
- `src/routeEditor/RouteEditorGLWidget.cpp:920`

### `objectSelected(QVector<GameObj*> obj)`
Same conclusion:
- `setSelectedObj(NULL)` only belongs in the `obj.size() == 0` case.
- For non-empty vectors, selection can switch directly to the built group.

Additional cleanup:
- Current code calls `setSelectedObj(groupObj)` inside the loop for every object added.
- That should be reduced to a single call after the group has been fully built.

Relevant code:
- `src/routeEditor/RouteEditorGLWidget.cpp:1791`

## Delete / Undo Scope Decision
Current simplified decision:
- If the user deletes the selected child or makes an unrelated selection, the pinned custom-group mode may be destroyed/unpinned.
- We do not need to preserve the pinned subgroup through delete/undo in this task.

Why this helps:
- Delete usually marks objects as unloaded rather than freeing them immediately.
- Undo can replace live route pointers with cloned objects, which makes raw cached child pointers unsafe for persistent pinned groups.
- Since pinning will be cleared on delete/unrelated reselection, we can avoid the harder persistent-identity problem for now.

Relevant code:
- `src/tsre/world/Route.cpp:2171`
- `src/tsre/Undo.cpp:71`
- `src/tsre/world/Route.cpp:1878`

## Places Where Group Handling May Need Review Or Change
### Must review / likely change
- `RouteEditorGLWidget::objectSelected(GameObj* obj)`
  - move transient clear into `if(obj == NULL)`
  - allow direct selection of custom `GroupObj`
  - `src/routeEditor/RouteEditorGLWidget.cpp:1780`

- `RouteEditorGLWidget::objectSelected(QVector<GameObj*> obj)`
  - move transient clear into `if(obj.size() == 0)`
  - build group first, then call `setSelectedObj(...)` once
  - `src/routeEditor/RouteEditorGLWidget.cpp:1791`

- `RouteEditorWindow::showProperties(...)`
  - support `PinnedObject`
  - ignore self-originating custom subgroup selection while pinned
  - clear pin on unrelated selection
  - `src/routeEditor/RouteEditorWindow.cpp:765`

- `RouteEditorWindow::updateProperties(...)`
  - same pin logic as `showProperties(...)`
  - `src/routeEditor/RouteEditorWindow.cpp:786`

- `PropertiesGroup`
  - add child list UI
  - add `Select`
   - add `Select Similar`
  - add `Reselect This Group Object`
  - emit signals or messages needed for custom selection/pin flow
  - `src/routeEditor/properties/PropertiesGroup.cpp`
  - `src/routeEditor/properties/PropertiesGroup.h`

- `RouteEditorGLWidget::editCopy()`
  - current group copy path uses `groupObj` directly instead of the actual selected group object
  - this is wrong if a custom `GroupObj` is selected
  - should copy from `selectedObj` cast to `GroupObj*`, not from the shared helper
  - `src/routeEditor/RouteEditorGLWidget.cpp:1824`

### Intentionally keep as-is for now
- `RouteEditorGLWidget::editFind(...)`
  - current behavior is intended
  - it collects objects into the widget's main shared `groupObj`
  - no change required for this task unless later UX requires different behavior
  - `src/routeEditor/RouteEditorGLWidget.cpp:1976`

- `RouteEditorGLWidget::editPaste()`
  - current behavior pastes copied groups into the shared working `groupObj`
  - may remain acceptable for now
  - review later only if custom-group copy/paste semantics need to preserve custom group identity
  - `src/routeEditor/RouteEditorGLWidget.cpp:1852`

### Worth sanity-checking during implementation
- Ctrl-click additive world-object selection in `handleSelection()`
  - this path explicitly pushes selection into shared `groupObj`
  - if a custom subgroup is currently selected, ctrl-add may implicitly discard that custom group and start using the shared helper
  - likely acceptable if pin/custom mode is meant to end on unrelated selection, but should be tested
  - `src/routeEditor/RouteEditorGLWidget.cpp:896`

## Suggested Implementation Steps
1. Update `objectSelected(GameObj* obj)` to avoid transient `NULL` on normal reselection.
2. Update `objectSelected(QVector<GameObj*> obj)` the same way and collapse repeated `setSelectedObj(...)` calls to one call.
3. Extend `PropertiesGroup` with:
   - child list
   - selected-child actions
   - reselect button
4. Add properties-side pin support in `RouteEditorWindow::showProperties(...)` and `updateProperties(...)`.
5. Introduce a custom subgroup `GroupObj` path for property-originated subgroup selection.
6. Fix `editCopy()` so group copy uses the actual selected group object, not always the shared helper.
7. Test selection transitions:
   - main group -> custom subgroup -> pinned main group stays visible
   - reselect pinned group
   - unrelated click clears pin
   - delete clears pin
   - copy selected custom subgroup

## Acceptance Criteria
- `PropertiesGroup` shows child objects for the selected main group.
- `Select` selects the chosen child object normally.
- `Select Similar` creates a subgroup using the same similarity rule as current `Select Similar`.
- While custom subgroup mode is active, the properties panel stays on the original main group.
- `Reselect This Group Object` restores normal selection of the pinned group.
- Unrelated selection or delete clears the pin and returns to normal property behavior.
- Copying a selected custom subgroup uses that subgroup, not the unrelated shared `groupObj`.

## Out Of Scope
- Persisting pinned custom group state through undo.
- Stable identity reconstruction of pinned groups after undo pointer replacement.
- Redesigning `editFind(...)` away from the shared widget `groupObj`.
