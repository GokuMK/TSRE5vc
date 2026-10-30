#include <tsre/trains/CabView.h>
#include <QHash>
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

struct Reader {
    const SimisDocument &doc;

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
    void control(Item block, CabView::Control &c) const {
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
            } else if (doc.is(item, "numpositions")) {
                for (double p : numbers(item, 1))
                    c.positions.push_back(int(std::llround(p)));
            } else if (doc.is(item, "numvalues")) {
                c.values = numbers(item, 1);
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
