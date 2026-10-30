#include <tsre/trains/CabView.h>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <tsre/units/UnitConversion.h>

using Item = SimisDocument::Item;
using Units::Quantity;

namespace {

// Open Rails' parts that need no .cvf entry (ThreeDimentionCabViewer).
const char *const OutsideTypes[] = {
    "EXTERNALWIPERS", "MIRRORS", "LEFTDOOR", "RIGHTDOOR",
    "ORTS_ITEM1CONTINUOUS", "ORTS_ITEM2CONTINUOUS", "ORTS_ITEM1TWOSTATE", "ORTS_ITEM2TWOSTATE",
    "ORTS_EXTERNALLEFTWINDOWFRONT", "ORTS_EXTERNALRIGHTWINDOWFRONT",
    "ORTS_EXTERNALLEFTWINDOWREAR", "ORTS_EXTERNALRIGHTWINDOWREAR"};

// The frame tables of a control as written, in file order: Open Rails reads
// NumFrames, NumPositions and NumValues as they come, each using what came
// before.
struct Table {
    enum Kind { Frames, Positions, Values } kind;
    // The block's numbers, its count first.
    std::vector<double> numbers;
};

struct Reader {
    const SimisDocument &doc;
    // The control being read's tables and a display's switch values.
    std::vector<Table> tables;
    std::vector<double> switchValues;

    QString string(Item block) const { return doc.text(doc.values(block).value(0)).toString(); }
    double number(Item value) const {
        double n = 0.0;
        return doc.number(value, Quantity::Any, n) ? n : 0.0;
    }
    // The block's values as numbers, the first `skip` left out (a count).
    std::vector<double> numbers(Item block, int skip = 0) const {
        std::vector<double> out;
        const QVector<Item> values = doc.values(block);
        for (int i = skip; i < values.size(); ++i)
            out.push_back(number(values[i]));
        return out;
    }
    void control(Item block, CabView::Control &c) {
        tables.clear();
        switchValues.clear();
        for (const Item &item : doc.items(block)) {
            if (!doc.isBlock(item))
                continue;
            const QVector<Item> values = doc.values(item);
            if (doc.is(item, "type")) {
                c.type = doc.text(values.value(0)).toString().toUpper();
                c.typeStyle = doc.text(values.value(1)).toString().toUpper();
            } else if (doc.is(item, "position")) {
                const std::vector<double> p = numbers(item);
                for (size_t k = 0; k < 4 && k < p.size(); ++k)
                    c.position[k] = float(p[k]);
            } else if (doc.is(item, "scalerange")) {
                c.minValue = number(values.value(0));
                c.maxValue = number(values.value(1));
            } else if (doc.is(item, "units")) {
                c.units = string(item).toUpper();
            } else if (doc.is(item, "style")) {
                c.style = string(item).toUpper();
            } else if (doc.is(item, "numframes")) {
                c.frames = int(number(values.value(0)));
                c.framesX = int(number(values.value(1)));
                c.framesY = int(number(values.value(2)));
                tables.push_back({Table::Frames, numbers(item)});
            } else if (doc.is(item, "numpositions")) {
                for (double p : numbers(item, 1))
                    c.positions.push_back(int(std::llround(p)));
                tables.push_back({Table::Positions, numbers(item)});
            } else if (doc.is(item, "numvalues")) {
                c.values = numbers(item, 1);
                tables.push_back({Table::Values, numbers(item)});
            } else if (doc.is(item, "states")) {
                // A multi-state display: States ( count columns rows State (
                // Style ( n ) SwitchVal ( v ) ) ... ).
                c.frames = int(number(values.value(0)));
                c.framesX = int(number(values.value(1)));
                c.framesY = int(number(values.value(2)));
                for (const Item &state : doc.items(item))
                    if (doc.isBlock(state) && doc.is(state, "state"))
                        for (const Item &field : doc.items(state))
                            if (doc.isBlock(field) && doc.is(field, "switchval"))
                                switchValues.push_back(number(doc.values(field).value(0)));
            } else if (doc.is(item, "orientation")) {
                c.orientation = int(number(values.value(0)));
            } else if (doc.is(item, "dirincrease")) {
                c.direction = int(number(values.value(0)));
            } else if (doc.is(item, "mousecontrol")) {
                c.mouseControl = number(values.value(0)) != 0.0;
            } else if (doc.is(item, "ortslabel")) {
                c.label = string(item);
            } else if (doc.is(item, "graphic")) {
                c.graphic = string(item);
            }
        }
    }
};

bool isKind(const QString &kind, std::initializer_list<const char *> kinds) {
    for (const char *k : kinds)
        if (kind.compare(QLatin1String(k), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

// Controls Open Rails reads as CVCDiscrete: drawn in frames from their
// NumPositions and NumValues.
bool isDiscrete(const QString &kind) {
    return isKind(kind, {"Lever", "TwoState", "TriState", "MultiState", "CombinedControl", "CabSignalDisplay"});
}

// A discrete control's tables as Open Rails' CVCDiscrete makes them: each
// block as it comes (positions shifted to start at 0, turned when written
// descending, values put at their positions), then, for a control with a
// graphic, missing positions and values filled so that every frame has a
// value, the values sorted.
void discreteTables(CabView::Control &c, const std::vector<Table> &tables) {
    std::vector<int> &positions = c.tablePositions;
    std::vector<double> &values = c.tableValues;
    int frames = 0, framesX = 0, framesY = 0, numPositions = 0, valuesRead = 0;
    bool canFill = true;
    for (const Table &t : tables) {
        const std::vector<double> &n = t.numbers;
        const auto at = [&](size_t i) { return i < n.size() ? n[i] : 0.0; };
        if (t.kind == Table::Frames) {
            frames = int(at(0));
            framesX = int(at(1));
            framesY = int(at(2));
        } else if (t.kind == Table::Positions) {
            const bool fill = positions.empty();
            numPositions = int(at(0));
            int least = 0;
            for (size_t i = 1; i < n.size(); ++i) {
                const int p = int(std::llround(n[i]));
                least = i == 1 ? p : std::min(least, p);
                if (fill)
                    positions.push_back(p);
            }
            if (least < 0)
                for (int &p : positions)
                    p -= least;
            // Positions written as huge numbers: numbered from 0.
            if (std::any_of(positions.begin(), positions.end(), [](int p) { return p > 0xFFFF; }))
                for (size_t i = 0; i < positions.size(); ++i)
                    positions[i] = int(i);
            if (!positions.empty() && positions.front() > positions.back()) {
                c.reversed = !c.reversed;
                const int most = *std::max_element(positions.begin(), positions.end());
                for (int &p : positions)
                    p = most - p;
            }
            if (positions.size() > 1 && positions[0] != 0)
                canFill = false;
            else
                for (size_t i = 1; i < positions.size(); ++i)
                    if (positions[i] <= positions[i - 1]) {
                        canFill = false;
                        break;
                    }
            // Positions past the last frame, counted down from it.
            if (positions.size() > 1 && canFill && int(positions.size()) < frames && positions.back() >= frames
                    && positions[0] == 0) {
                positions.back() = frames - 1;
                for (int i = int(positions.size()) - 2; i >= 1; --i) {
                    if (positions[size_t(i)] >= frames || positions[size_t(i)] >= positions[size_t(i) + 1])
                        positions[size_t(i)] = positions[size_t(i) + 1] - 1;
                    else
                        break;
                }
            }
        } else {
            const double numValues = at(0);
            for (size_t i = 1; i < n.size(); ++i) {
                while (int(positions.size()) <= valuesRead)
                    positions.push_back(valuesRead);
                if (numValues != numPositions) {
                    const int place = positions[size_t(valuesRead)];
                    if (place >= 0) {
                        while (int(values.size()) <= place)
                            values.push_back(0.0);
                        values[size_t(place)] = n[i];
                    }
                }
                values.push_back(n[i]);
                ++valuesRead;
            }
        }
    }
    c.tableFrames = frames;
    // Without a graphic Open Rails leaves the tables as they are.
    if (c.graphic.trimmed().isEmpty())
        return;
    if (valuesRead > 0 || c.style == "SPRUNG" || c.style == "NOT_SPRUNG" || frames > 0 || (framesX > 0 && framesY > 0)) {
        if (frames == 0) {
            // No frames: Open Rails drops the graphic, the tables as they are.
            if (framesX == 0 || framesY == 0)
                return;
            frames = framesX * framesY;
        }
        c.tableFrames = frames;
        if (values.size() != positions.size() || (int(values.size()) < frames && canFill)
                || (!values.empty() && values.front() == values.back() && values.front() == 0.0)) {
            const double offset = 1.0 / (2.0 * frames);
            if (int(positions.size()) != valuesRead || values.size() <= 1) {
                // The two ends only: every frame between, evenly.
                positions = {0, frames - 1};
                values.assign(size_t(std::max(frames - 1, 1)), 0.0);
                values[0] = c.minValue + offset;
                values.push_back(c.maxValue - offset);
            } else if (values.size() == 2 && values[0] == 0.0 && values[1] < c.maxValue && positions[0] == 0
                       && positions[1] == 1 && int(values.size()) < frames) {
                positions.push_back(frames - 1);
                while (int(values.size()) < frames - 1)
                    values.push_back(0.0);
                values.push_back(c.maxValue - offset);
            } else {
                // A value for each position skipped.
                size_t place = 1;
                for (int i = 1; i < frames && i < int(positions.size()) && int(values.size()) < frames; ++i) {
                    int skipped = positions[size_t(i)] - positions[size_t(i) - 1];
                    while (skipped > 1 && int(values.size()) < frames && place <= values.size()) {
                        values.insert(values.begin() + long(place), 0.0);
                        ++place;
                        --skipped;
                    }
                    ++place;
                }
            }
            // Values between two positions, in a straight line.
            if (!values.empty()
                    && ((int(positions.size()) < frames - 1 && values.front() <= values.back())
                        || (values.size() > 1 && values.front() == values[values.size() - 2] && values.front() == 0.0))) {
                int p = 0;
                for (size_t i = 1; i < positions.size(); ++i) {
                    const int to = positions[i];
                    if (to != p + 1 && to > 0 && size_t(to) < values.size() && size_t(p) < values.size())
                        for (int j = p + 1; j < to; ++j)
                            values[size_t(j)] = values[size_t(p)] + (values[size_t(to)] - values[size_t(p)]) * double(j) / to;
                    p = to;
                }
            }
        }
    }
    if (!values.empty() && values.front() > values.back())
        c.reversed = !c.reversed;
    std::sort(values.begin(), values.end());
}

// A multi-state display's: its states' switch values, filled to its frames
// with the last, sorted (CVCMultiStateDisplay).
void displayTables(CabView::Control &c, std::vector<double> values) {
    c.tableFrames = c.frames;
    if (!values.empty() && values.front() > values.back())
        c.reversed = true;
    while (!values.empty() && int(values.size()) < c.frames)
        values.push_back(values.back());
    std::sort(values.begin(), values.end());
    c.tableValues = std::move(values);
}

// The value's index in a lever's values: the nearest (Open Rails'
// PercentToIndex), a per cent taken as a fraction.
int percentToIndex(const CabView::Control &c, double percent) {
    if (percent > 1.0)
        percent /= 100.0;
    if (c.minValue != c.maxValue && !(c.minValue == 0.0 && c.maxValue == 0.0))
        percent = std::clamp(percent, std::min(c.minValue, c.maxValue), std::max(c.minValue, c.maxValue));
    const std::vector<double> &v = c.tableValues;
    if (v.size() > 1) {
        size_t i = size_t(std::lower_bound(v.begin(), v.end(), percent) - v.begin());
        if (i > v.size() - 1)
            i = v.size() - 1;
        if (i > 0 && std::fabs(v[i - 1] - percent) < std::fabs(v[i] - percent))
            --i;
        return c.reversed ? int(v.size() - 1 - i) : int(i);
    }
    if (c.maxValue != c.minValue)
        return int(percent / (c.maxValue - c.minValue) * c.tableFrames);
    return 0;
}

// Open Rails' types by how a value picks their frame (GetDrawIndex).
const QSet<QString> &leverTypes() {
    static const QSet<QString> types{
        "ENGINE_BRAKE", "BRAKEMAN_BRAKE", "TRAIN_BRAKE", "REGULATOR", "CUTOFF", "BLOWER", "DAMPERS_FRONT", "STEAM_HEAT",
        "STEAM_BOOSTER_AIR", "STEAM_BOOSTER_IDLE", "STEAM_BOOSTER_LATCH", "ORTS_WATER_SCOOP", "WATER_INJECTOR1",
        "WATER_INJECTOR2", "SMALL_EJECTOR", "ORTS_LARGE_EJECTOR", "FIREHOLE", "THROTTLE", "THROTTLE_DISPLAY",
        "DYNAMIC_BRAKE_DISPLAY",
        // The dynamic brake's notch and the combined handle's split are
        // their controllers' (not kept here): by their values too.
        "DYNAMIC_BRAKE", "CPH_DISPLAY", "CP_HANDLE"};
    return types;
}
const QSet<QString> &stateTypes() {
    static const QSet<QString> types{
        "ALERTER_DISPLAY", "RESET", "WIPERS", "EXTERNALWIPERS", "LEFTDOOR", "RIGHTDOOR", "MIRRORS", "ORTS_LEFTWINDOW",
        "ORTS_RIGHTWINDOW", "HORN", "VACUUM_EXHAUSTER", "WHISTLE", "BELL", "SANDERS", "SANDING", "WHEELSLIP",
        "FRONT_HLIGHT", "PANTOGRAPH", "PANTOGRAPH2", "ORTS_PANTOGRAPH3", "ORTS_PANTOGRAPH4", "PANTOGRAPHS_4",
        "PANTOGRAPHS_4C", "PANTOGRAPHS_5", "PANTO_DISPLAY", "ORTS_VOLTAGE_SELECTOR", "ORTS_PANTOGRAPH_SELECTOR",
        "ORTS_POWER_LIMITATION_SELECTOR", "ORTS_CIRCUIT_BREAKER_DRIVER_CLOSING_ORDER",
        "ORTS_CIRCUIT_BREAKER_DRIVER_OPENING_ORDER", "ORTS_CIRCUIT_BREAKER_DRIVER_CLOSING_AUTHORIZATION",
        "ORTS_CIRCUIT_BREAKER_STATE", "ORTS_CIRCUIT_BREAKER_CLOSED", "ORTS_CIRCUIT_BREAKER_OPEN",
        "ORTS_CIRCUIT_BREAKER_AUTHORIZED", "ORTS_CIRCUIT_BREAKER_OPEN_AND_AUTHORIZED",
        "ORTS_TRACTION_CUT_OFF_RELAY_DRIVER_CLOSING_ORDER", "ORTS_TRACTION_CUT_OFF_RELAY_DRIVER_OPENING_ORDER",
        "ORTS_TRACTION_CUT_OFF_RELAY_DRIVER_CLOSING_AUTHORIZATION", "ORTS_TRACTION_CUT_OFF_RELAY_STATE",
        "ORTS_TRACTION_CUT_OFF_RELAY_CLOSED", "ORTS_TRACTION_CUT_OFF_RELAY_OPEN",
        "ORTS_TRACTION_CUT_OFF_RELAY_AUTHORIZED", "ORTS_TRACTION_CUT_OFF_RELAY_OPEN_AND_AUTHORIZED", "DIRECTION",
        "DIRECTION_DISPLAY", "ASPECT_DISPLAY", "GEARS", "OVERSPEED", "PENALTY_APP", "EMERGENCY_BRAKE", "ORTS_BAILOFF",
        "ORTS_QUICKRELEASE", "ORTS_OVERCHARGE", "DOORS_DISPLAY", "CYL_COCKS", "ORTS_BLOWDOWN_VALVE", "ORTS_CYL_COMP",
        "STEAM_INJ1", "STEAM_INJ2", "GEARS_DISPLAY", "CAB_RADIO", "ORTS_PLAYER_DIESEL_ENGINE",
        "ORTS_HELPERS_DIESEL_ENGINES", "ORTS_PLAYER_DIESEL_ENGINE_STATE", "ORTS_PLAYER_DIESEL_ENGINE_STARTER",
        "ORTS_PLAYER_DIESEL_ENGINE_STOPPER", "ORTS_CABLIGHT", "ORTS_LEFTDOOR", "ORTS_RIGHTDOOR", "ORTS_MIRRORS",
        "ORTS_BATTERY_SWITCH_COMMAND_SWITCH", "ORTS_BATTERY_SWITCH_COMMAND_BUTTON_CLOSE",
        "ORTS_BATTERY_SWITCH_COMMAND_BUTTON_OPEN", "ORTS_BATTERY_SWITCH_ON", "ORTS_MASTER_KEY",
        "ORTS_CURRENT_CAB_IN_USE", "ORTS_OTHER_CAB_IN_USE", "ORTS_SERVICE_RETENTION_BUTTON",
        "ORTS_SERVICE_RETENTION_CANCELLATION_BUTTON", "ORTS_ELECTRIC_TRAIN_SUPPLY_COMMAND_SWITCH",
        "ORTS_ELECTRIC_TRAIN_SUPPLY_ON", "ORTS_ODOMETER_DIRECTION", "ORTS_ODOMETER_RESET", "ORTS_GENERIC_ITEM1",
        "ORTS_GENERIC_ITEM2", "ORTS_EOT_EMERGENCY_BRAKE",
        // Open Rails' displays of numbers in frames and scripted controls.
        "ORTS_TCS", "ORTS_POWER_SUPPLY", "ORTS_RESTRICTED_SPEED_ZONE_ACTIVE", "ORTS_SELECTED_SPEED_REGULATOR_MODE",
        "ORTS_SELECTED_SPEED_MAXIMUM_ACCELERATION", "ORTS_NUMBER_OF_AXES_DISPLAY_UNITS",
        "ORTS_NUMBER_OF_AXES_DISPLAY_TENS", "ORTS_NUMBER_OF_AXES_DISPLAY_HUNDREDS", "ORTS_TRAIN_LENGTH_METERS",
        "ORTS_REMAINING_TRAIN_LENGTH_SPEED_RESTRICTED", "ORTS_REMAINING_TRAIN_LENGTH_PERCENT", "ORTS_MOTIVE_FORCE",
        "ORTS_MOTIVE_FORCE_KILONEWTON", "ORTS_MAXIMUM_FORCE", "ORTS_SELECTED_SPEED",
        "ORTS_FORCE_IN_PERCENT_THROTTLE_AND_DYNAMIC_BRAKE", "ORTS_TRAIN_TYPE_PAX_OR_CARGO", "ORTS_CONTROLLER_VOLTAGE",
        "ORTS_AMPERS_BY_CONTROLLER_VOLTAGE", "ORTS_CC_SELECTED_SPEED", "ORTS_MULTI_POSITION_CONTROLLER",
        "ORTS_ACCELERATION_IN_TIME", "ORTS_CC_SPEED_DELTA", "ORTS_CC_SPEED_0"};
    return types;
}
const QSet<QString> &buttonTypes() {
    static const QSet<QString> types{"ORTS_SCREEN_SELECT", "ORTS_DP_MOVE_TO_BACK", "ORTS_DP_MOVE_TO_FRONT",
                                     "ORTS_DP_TRACTION",   "ORTS_DP_IDLE",         "ORTS_DP_BRAKE",
                                     "ORTS_DP_MORE",       "ORTS_DP_LESS",         "ORTS_EOT_COMM_TEST",
                                     "ORTS_EOT_DISARM",    "ORTS_EOT_ARM_TWO_WAY"};
    return types;
}

}

float CabView::frame(const Control &c, double value, float lastFrame) {
    if (isKind(c.kind, {"Dial", "Gauge"})) {
        if (c.maxValue == c.minValue || lastFrame <= 0.0f)
            return 0.0f;
        const double share = std::clamp((value - c.minValue) / (c.maxValue - c.minValue), 0.0, 1.0);
        return float(share) * lastFrame;
    }
    const bool display = isKind(c.kind, {"MultiStateDisplay"});
    if (!display && !isDiscrete(c.kind))
        return -1.0f;
    int index = 0;
    if (leverTypes().contains(c.type)) {
        index = percentToIndex(c, value);
    } else if (c.type == "FRICTION_BRAKING") {
        index = value > 0.001 ? 1 : 0;
    } else if (stateTypes().contains(c.type)) {
        index = int(value);
    } else if (buttonTypes().contains(c.type)) {
        index = value != 0.0 ? 1 : 0;
    } else if (c.type == "ORTS_STATIC_DISPLAY") {
        index = 0;
    } else if (c.type == "ORTS_EOT_STATE_DISPLAY") {
        const auto above = std::find_if(c.tableValues.begin(), c.tableValues.end(),
                                        [&](double v) { return v > double(int(value)); });
        index = above == c.tableValues.end() ? int(c.tableValues.size()) - 1 : int(above - c.tableValues.begin()) - 1;
    } else if (c.type == "ORTS_SELECTED_SPEED_SELECTOR") {
        const double share = c.maxValue != 0.0 ? value / c.maxValue : 0.0;
        index = std::clamp(int(share * (c.tableFrames - 1)), 0, std::max(c.tableFrames - 1, 0));
    } else if (c.type == "ORTS_SELECTED_SPEED_DISPLAY") {
        index = int(value) / 10;
    } else {
        return -1.0f;
    }
    // A position's frame: where the positions and values pair up.
    if (!display && !isKind(c.kind, {"CabSignalDisplay"}) && index >= 0 && size_t(index) < c.tablePositions.size()
            && c.tablePositions.size() == c.tableValues.size())
        index = c.tablePositions[size_t(index)];
    if (index >= c.tableFrames)
        index = c.tableFrames - 1;
    return float(std::max(index, 0));
}

std::shared_ptr<const CabView> CabView::read(const QString &path, const SimisDocument::Roots &roots) {
    auto view = std::make_shared<CabView>();
    view->file = path;
    const std::shared_ptr<const SimisDocument> doc = SimisDocument::read(path, roots);
    view->found = doc->problems();
    if (!doc->ok())
        return view;
    view->read_ = true;
    Reader reader{*doc};
    QHash<QString, int> orders;
    // Open Rails' controls, then its own extra ones, each kind in file
    // order.
    QVector<Item> blocks = doc->all("tr_cabviewfile(cabviewcontrols");
    blocks += doc->all("tr_cabviewfile(ortscabviewcontrols");
    for (const Item &block : blocks) {
        for (const Item &item : doc->items(block)) {
            if (!doc->isBlock(item))
                continue;
            Control c;
            c.kind = doc->text(item).toString();
            reader.control(item, c);
            if (isDiscrete(c.kind))
                discreteTables(c, reader.tables);
            else if (isKind(c.kind, {"MultiStateDisplay"}))
                displayTables(c, reader.switchValues);
            if (c.type.isEmpty()) {
                view->found.push_back({doc->file(item), doc->line(item), QString("%1 without a Type").arg(c.kind)});
                continue;
            }
            c.order = orders.value(c.type, 0);
            orders[c.type] = c.order + 1;
            view->list.push_back(std::move(c));
        }
    }
    return view;
}

int CabView::find(QStringView type, int order) const {
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].order == order && type.compare(list[i].type, Qt::CaseInsensitive) == 0)
            return int(i);
    return -1;
}

bool CabParts::isOutside(QStringView type) {
    for (const char *outside : OutsideTypes)
        if (type.compare(QLatin1String(outside), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

const CabParts::Control *CabParts::of(int control) const {
    for (const Control &c : controls)
        if (c.control == control)
            return &c;
    return nullptr;
}

CabParts CabParts::match(const CabView *view, const QStringList &names, const std::vector<float> &lastFrames) {
    CabParts parts;
    for (int i = 0; i < names.size(); ++i) {
        // TYPE:order:p1:p2-subpart; anything else is not a cab part.
        const QString name = names[i].trimmed().toUpper();
        const QStringList fields = name.section(u'-', 0, 0).split(u':');
        bool isOrder = false;
        const int order = fields.size() >= 2 ? fields[1].trimmed().toInt(&isOrder) : 0;
        if (!isOrder)
            continue;
        const QString type = fields[0].trimmed();
        const float last = size_t(i) < lastFrames.size() ? lastFrames[size_t(i)] : -1.0f;
        if (isOutside(type)) {
            auto at = std::find_if(parts.outside.begin(), parts.outside.end(),
                                   [&](const Outside &o) { return o.type == type && o.order == order; });
            if (at == parts.outside.end())
                at = parts.outside.insert(parts.outside.end(), Outside{type, order, {}});
            at->parts.push_back(i);
            continue;
        }
        const int control = view != nullptr ? view->find(type, order) : -1;
        if (control < 0) {
            parts.unmatched << names[i];
            continue;
        }
        auto at = std::find_if(parts.controls.begin(), parts.controls.end(),
                               [&](const Control &c) { return c.control == control; });
        if (at == parts.controls.end()) {
            Control c;
            c.control = control;
            c.parameter1 = fields.value(2).trimmed();
            c.parameter2 = fields.value(3).trimmed();
            at = parts.controls.insert(parts.controls.end(), c);
        }
        at->parts.push_back(i);
        at->lastFrame = std::max(at->lastFrame, last);
    }
    return parts;
}
