#include <routeEditor/activity/OrTagsPicker.h>
#include <tsre/trains/TrainCars.h>

namespace {

// An item's tag (a heading has none).
constexpr int TagRole = Qt::UserRole;

}

const QVector<OrTagsPicker::Recommended> &OrTagsPicker::RecommendedTags() {
    // Made on the first call: the translations are loaded by then.
    static const QVector<Recommended> tags = {
        //% "First class"
        {"A", ClassGroup, qtTrId("route.editor.activity.or.tags.a")},
        //% "Second class"
        {"B", ClassGroup, qtTrId("route.editor.activity.or.tags.b")},
        //% "First and second class"
        {"AB", ClassGroup, qtTrId("route.editor.activity.or.tags.ab")},
        //% "Restaurant car"
        {"WR", ClassGroup, qtTrId("route.editor.activity.or.tags.wr")},
        //% "Sleeping car"
        {"WL", ClassGroup, qtTrId("route.editor.activity.or.tags.wl")},
        //% "Couchette car"
        {"BC", ClassGroup, qtTrId("route.editor.activity.or.tags.bc")},
        //% "Luggage van"
        {"D", ClassGroup, qtTrId("route.editor.activity.or.tags.d")},
        //% "Bicycle spaces"
        {"bike", FeatureGroup, qtTrId("route.editor.activity.or.tags.bike")},
        //% "Wheelchair spaces"
        {"wheelchair", FeatureGroup, qtTrId("route.editor.activity.or.tags.wheelchair")},
        //% "Quiet zone"
        {"quiet", FeatureGroup, qtTrId("route.editor.activity.or.tags.quiet")},
        //% "Family compartment"
        {"family", FeatureGroup, qtTrId("route.editor.activity.or.tags.family")},
        //% "Conductor's compartment"
        {"conductor", FeatureGroup, qtTrId("route.editor.activity.or.tags.conductor")},
    };
    return tags;
}

bool OrTagsPicker::IsRecommended(const QString &tag) {
    for (const Recommended &recommended : RecommendedTags())
        if (recommended.tag == tag)
            return true;
    return false;
}

OrTagsPicker::OrTagsPicker(const QString &title, const QString &tags, const QStringList &own, QWidget *parent)
    : QDialog(parent) {
    setObjectName("orTagsPicker");
    setWindowTitle(
        //% "Tags: %1"
        qtTrId("route.editor.activity.or.tags.title").arg(title));
    const QStringList written = tags.split(' ', Qt::SkipEmptyParts);

    list.setObjectName("orTagList");
    list.setColumnCount(2);
    list.setHeaderLabels({
        //% "Tag"
        qtTrId("route.editor.activity.or.tags.column.tag"),
        //% "What it means"
        qtTrId("route.editor.activity.or.tags.column.description")});
    list.setRootIsDecorated(false);
    list.setIndentation(10);
    list.setUniformRowHeights(true);
    list.header()->resizeSection(0, 140);
    QFont bold = list.font();
    bold.setBold(true);
    auto heading = [this, &bold](const QString &text) {
        QTreeWidgetItem *item = new QTreeWidgetItem(&list, {text});
        item->setFont(0, bold);
        item->setFlags(Qt::ItemIsEnabled);
        item->setFirstColumnSpanned(true);
        return item;
    };
    QTreeWidgetItem *heads[2] = {
        //% "Class and type (UIC letters)"
        heading(qtTrId("route.editor.activity.or.tags.group.class")),
        //% "Features"
        heading(qtTrId("route.editor.activity.or.tags.group.features"))};
    for (const Recommended &recommended : RecommendedTags()) {
        QTreeWidgetItem *item = new QTreeWidgetItem(heads[recommended.group], {recommended.tag, recommended.description});
        item->setData(0, TagRole, recommended.tag);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, written.contains(recommended.tag) ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(1, recommended.description);
    }
    // The own: the car's in their order, then the timetable's others.
    //% "Own tags"
    ownHead = heading(qtTrId("route.editor.activity.or.tags.group.own"));
    for (const QString &tag : written)
        if (!IsRecommended(tag) && itemOf(tag) == nullptr)
            addOwn(tag, true);
    for (const QString &tag : own)
        if (!IsRecommended(tag) && itemOf(tag) == nullptr)
            addOwn(tag, false);
    list.expandAll();

    added.setObjectName("orTagNew");
    //% "A tag of your own: a word"
    added.setPlaceholderText(qtTrId("route.editor.activity.or.tags.new"));
    QPushButton *add = new QPushButton(
        //% "Add"
        qtTrId("route.editor.activity.or.tags.add"));
    add->setObjectName("orTagAdd");
    add->setAutoDefault(false);
    result.setObjectName("orTagsText");
    result.setReadOnly(true);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    // Top down: the tags, a new one, the text they make.
    QHBoxLayout *addLine = new QHBoxLayout;
    addLine->addWidget(&added, 1);
    addLine->addWidget(add);
    QHBoxLayout *resultLine = new QHBoxLayout;
    resultLine->addWidget(new QLabel(
        //% "Tags:"
        qtTrId("route.editor.activity.or.tags.result")));
    resultLine->addWidget(&result, 1);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(&list, 1);
    layout->addLayout(addLine);
    layout->addLayout(resultLine);
    layout->addWidget(buttons);
    resize(480, 640);

    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QObject::connect(add, &QPushButton::clicked, this, &OrTagsPicker::addTyped);
    QObject::connect(&added, &QLineEdit::returnPressed, this, &OrTagsPicker::addTyped);
    QObject::connect(&list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *, int column) {
        if (column == 0)
            showResult();
    });
    showResult();
}

QString OrTagsPicker::text() const {
    // The recommended first, in their order, then the own in theirs.
    QStringList out;
    for (int h = 0; h < list.topLevelItemCount(); ++h)
        for (int i = 0; i < list.topLevelItem(h)->childCount(); ++i) {
            const QTreeWidgetItem *item = list.topLevelItem(h)->child(i);
            if (item->checkState(0) == Qt::Checked)
                out << item->data(0, TagRole).toString();
        }
    return out.join(' ');
}

QTreeWidgetItem *OrTagsPicker::itemOf(const QString &tag) const {
    for (int h = 0; h < list.topLevelItemCount(); ++h)
        for (int i = 0; i < list.topLevelItem(h)->childCount(); ++i)
            if (list.topLevelItem(h)->child(i)->data(0, TagRole).toString() == tag)
                return list.topLevelItem(h)->child(i);
    return nullptr;
}

QTreeWidgetItem *OrTagsPicker::addOwn(const QString &tag, bool on) {
    QTreeWidgetItem *item = new QTreeWidgetItem(ownHead, {tag});
    item->setData(0, TagRole, tag);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    item->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
    return item;
}

void OrTagsPicker::addTyped() {
    const QString tag = TrainCar::Word(added.text());
    if (tag.isEmpty())
        return;
    if (QTreeWidgetItem *item = itemOf(tag)) {
        item->setCheckState(0, Qt::Checked);
        list.setCurrentItem(item);
    } else {
        list.setCurrentItem(addOwn(tag, true));
        ownHead->setExpanded(true);
    }
    added.clear();
    showResult();
}

void OrTagsPicker::showResult() {
    result.setText(text());
}
