#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>
#include <tsre/fileFunctions/SimisDocument.h>

// A cab view file (.cvf) as task driving 08 reads it for Open Rails' 3D
// cabs: its controls in file order, each as written, numbers in the
// control's own units (ScaleRange's, Units'). Open Rails' CabViewFile is
// the functional reference. Made once, never changed after, shared by const
// pointer between the editor and the simulation thread.
class CabView {
  public:
    struct Control {
        // The block's name, its kind as written: Dial, Lever, TwoState,
        // TriState, MultiState, MultiStateDisplay, CombinedControl, Gauge,
        // Digital, DigitalClock, CabSignalDisplay, ScreenDisplay ...
        QString kind;
        // Type ( THROTTLE LEVER ): the type and the word after it, in upper
        // case.
        QString type;
        QString typeStyle;
        // The nth control of its type in the file: with the type, the key
        // a 3D cab's part names it by (TYPE:order).
        int order = 0;
        // Position ( x y width height ), the 2D cab's pixels.
        float position[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        // ScaleRange ( min max ).
        double minValue = 0.0;
        double maxValue = 0.0;
        // Units ( KM_PER_HOUR ) and Style ( POINTER ), in upper case.
        QString units;
        QString style;
        // NumFrames ( count columns rows ).
        int frames = 0;
        int framesX = 0;
        int framesY = 0;
        // NumPositions and NumValues, their entries as written (the count
        // before them left out).
        std::vector<int> positions;
        std::vector<double> values;
        int orientation = 0;
        int direction = 0;
        bool mouseControl = false;
        // ORTSLabel: the name Open Rails shows for it; empty for none.
        QString label;
        // Graphic, as written.
        QString graphic;
        // The tables a value's frame is found in, as Open Rails makes them
        // for a control drawn in frames (CVCDiscrete, CVCMultiStateDisplay):
        // the positions from 0 and ascending, the values ascending (a
        // multi-state display's from its states' SwitchVal), missing ones
        // filled; reversed when written the other way; the frames counted.
        std::vector<int> tablePositions;
        std::vector<double> tableValues;
        bool reversed = false;
        int tableFrames = 0;
    };

    // Reads the file at `path`: problems() tell what was wrong, ok()
    // whether it was read.
    static std::shared_ptr<const CabView> read(const QString &path, const SimisDocument::Roots &roots);

    bool ok() const { return read_; }
    const QString &path() const { return file; }
    const std::vector<Control> &controls() const { return list; }
    const QVector<SimisDocument::Problem> &problems() const { return found; }
    // The control of a type (any case) and order, -1 for none.
    int find(QStringView type, int order) const;
    // The key frame a 3D cab's part of the control shows for `value` (in
    // the control's units; a lever's as a fraction or a per cent), as Open
    // Rails poses it (AnimatedPartMultiState): a control drawn in frames by
    // its type's rule through its tables (GetDrawIndex); a dial or gauge at
    // the value's share of ScaleRange times `lastFrame`, the part's last key
    // frame. -1 where Open Rails keeps the frame it had (a type with no
    // rule) or draws the control otherwise (digits, screens).
    static float frame(const Control &control, double value, float lastFrame);

  private:
    QString file;
    bool read_ = false;
    std::vector<Control> list;
    QVector<SimisDocument::Problem> found;
};

// A 3D cab shape's parts matched to its .cvf's controls as Open Rails
// matches them (ThreeDimentionCabViewer): a part named TYPE:order, with up
// to two parameters (TYPE:order:p1:p2) and a sub-part after a dash
// (TYPE:order:p1-2), names in any case. A control may have several parts.
struct CabParts {
    struct Control {
        // Its index in CabView::controls().
        int control = -1;
        // Its parts (the shape's part indices), in shape order.
        std::vector<int> parts;
        // The last key frame of its parts (-1: none has keys).
        float lastFrame = -1.0f;
        // The first part's parameters, as written.
        QString parameter1;
        QString parameter2;
    };
    // An outside part, which needs no .cvf entry (wipers, mirrors, doors,
    // windows, Open Rails' items).
    struct Outside {
        QString type;
        int order = 0;
        std::vector<int> parts;
    };
    std::vector<Control> controls;
    std::vector<Outside> outside;
    // Part names of the TYPE:order form that no control of the .cvf has.
    QStringList unmatched;

    // Matches the shape's part names (with each part's last key frame, -1
    // without keys) to `view`'s controls; `view` may be null (no .cvf: all
    // but the outside parts unmatched).
    static CabParts match(const CabView *view, const QStringList &names, const std::vector<float> &lastFrames);
    // Whether a type is an outside part's.
    static bool isOutside(QStringView type);
    // The entry of a control, or null.
    const Control *of(int control) const;
};
