#include <routeEditor/activity/OrCommandPicker.h>
#include <tsre/gui/GuiFunct.h>
#include <tsre/trains/OrTimetable.h>

namespace {

// An item's choice (its index), none for a heading.
constexpr int ChoiceRole = Qt::UserRole;
const QColor NotRunColour(200, 120, 0);

QString notRunText() {
    //% "Not run by TSRE yet: kept in the timetable for Open Rails."
    return qtTrId("route.editor.activity.or.commands.not.run");
}

}

QString OrCommandPicker::Choice::text() const {
    if (spec == nullptr)
        return "$" + typed;
    QString out = "$" + spec->name;
    if (!value.trimmed().isEmpty())
        out += "=" + value.trimmed();
    for (const Qualifier &qualifier : qualifiers)
        out += " /" + qualifier.name + (qualifier.value.trimmed().isEmpty() ? QString() : "=" + qualifier.value.trimmed());
    return out;
}

OrCommandPicker::OrCommandPicker(int place, const QString &title, const QString &text, const QStringList &trains,
                                 const QStringList &paths, const QString &unit, QWidget *parent)
    : QDialog(parent), trainNames(trains), pathNames(paths), speedUnit(unit) {
    setObjectName("orCommandPicker");
    setWindowTitle(
        //% "Commands: %1"
        qtTrId("route.editor.activity.or.commands.title").arg(title));
    // The commands Open Rails takes here; those written fill them, in their
    // order, a second of a kind or one not taken here kept as typed.
    for (const OrCommandSpec &spec : OrCommandSpecs())
        if ((spec.places & place) != 0) {
            Choice choice;
            choice.spec = &spec;
            choices << choice;
        }
    for (const OrTimetable::Command &command : OrTimetable::Commands(OrTimetable::TypedCommands(text))) {
        int k = -1;
        for (int i = 0; i < choices.size() && k < 0; ++i)
            if (choices[i].spec != nullptr && choices[i].spec->name == command.name && !choices[i].on)
                k = i;
        if (k < 0) {
            Choice typed;
            typed.typed = command.text;
            choices << typed;
            k = choices.size() - 1;
        }
        Choice &choice = choices[k];
        choice.on = true;
        choice.order = nextOrder++;
        // The values as written (the command read is in lower case): its
        // parts split as Open Rails splits them, at '/' and the first '='.
        const QStringList parts = command.text.split('/');
        const int equals = parts[0].indexOf('=');
        choice.value = equals >= 0 ? parts[0].mid(equals + 1).trimmed() : QString();
        for (int i = 0; i < command.qualifiers.size() && i + 1 < parts.size(); ++i) {
            const int at = parts[i + 1].indexOf('=');
            choice.qualifiers << Qualifier{command.qualifiers[i].name, at >= 0 ? parts[i + 1].mid(at + 1).trimmed() : QString()};
        }
    }

    list.setObjectName("orCommandList");
    list.setColumnCount(2);
    list.setHeaderLabels({
        //% "Command"
        qtTrId("route.editor.activity.or.commands.column.command"),
        //% "What it does"
        qtTrId("route.editor.activity.or.commands.column.description")});
    list.setRootIsDecorated(false);
    list.setIndentation(10);
    list.setUniformRowHeights(true);
    list.header()->resizeSection(0, 180);
    QFont bold = list.font();
    bold.setBold(true);
    auto heading = [this, &bold](const QString &text) {
        QTreeWidgetItem *item = new QTreeWidgetItem(&list, {text});
        item->setFont(0, bold);
        item->setFlags(Qt::ItemIsEnabled);
        item->setFirstColumnSpanned(true);
        return item;
    };
    auto add = [this](QTreeWidgetItem *parent, int k) {
        const Choice &choice = choices[k];
        QTreeWidgetItem *item = new QTreeWidgetItem(parent);
        item->setData(0, ChoiceRole, k);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, choice.on ? Qt::Checked : Qt::Unchecked);
        if (choice.spec != nullptr) {
            item->setText(0, "$" + choice.spec->name);
            item->setText(1, choice.spec->description);
            item->setToolTip(1, choice.spec->description);
            if (!OrTimetableCommandRun(choice.spec->name)) {
                item->setForeground(0, NotRunColour);
                item->setToolTip(0, notRunText());
            }
        } else {
            item->setText(0, "$" + choice.typed);
            item->setToolTip(0, item->text(0));
            //% "Not taken here by Open Rails, or a second one: kept as typed."
            item->setText(1, qtTrId("route.editor.activity.or.commands.typed"));
        }
    };
    for (int group = OrCommandSpec::StartGroup; group <= OrCommandSpec::DisposeGroup; ++group) {
        QTreeWidgetItem *head = nullptr;
        for (int k = 0; k < choices.size(); ++k)
            if (choices[k].spec != nullptr && choices[k].spec->group == group) {
                if (head == nullptr)
                    head = heading(OrCommandSpec::GroupName(OrCommandSpec::Group(group)));
                add(head, k);
            }
    }
    QTreeWidgetItem *typedHead = nullptr;
    for (int k = 0; k < choices.size(); ++k)
        if (choices[k].spec == nullptr) {
            if (typedHead == nullptr)
                //% "Kept as typed"
                typedHead = heading(qtTrId("route.editor.activity.or.commands.group.typed"));
            add(typedHead, k);
        }
    list.expandAll();

    detailsArea.setObjectName("orCommandDetails");
    detailsArea.setWidgetResizable(true);
    detailsArea.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailsArea.setMinimumHeight(80);
    result.setObjectName("orCommandText");
    result.setReadOnly(true);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    // Top down: the commands, the chosen one's details, the text they make.
    QSplitter *split = new QSplitter(Qt::Vertical);
    split->addWidget(&list);
    split->addWidget(&detailsArea);
    split->setChildrenCollapsible(false);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    QHBoxLayout *resultLine = new QHBoxLayout;
    resultLine->addWidget(new QLabel(
        //% "Commands:"
        qtTrId("route.editor.activity.or.commands.result")));
    resultLine->addWidget(&result, 1);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(split, 1);
    layout->addLayout(resultLine);
    layout->addWidget(buttons);
    resize(860, 640);
    split->setSizes({330, 230});

    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QObject::connect(&list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        const QVariant k = item->data(0, ChoiceRole);
        if (column == 0 && k.isValid())
            setOn(k.toInt(), item->checkState(0) == Qt::Checked);
    });
    QObject::connect(&list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        const QVariant k = item != nullptr ? item->data(0, ChoiceRole) : QVariant();
        showDetails(k.isValid() ? k.toInt() : -1);
    });
    // The first written, else the first there.
    int first = -1;
    for (int k = 0; k < choices.size(); ++k)
        if (choices[k].on && (first < 0 || choices[k].order < choices[first].order))
            first = k;
    if (first < 0 && !choices.isEmpty())
        first = 0;
    if (QTreeWidgetItem *item = first >= 0 ? itemOf(first) : nullptr)
        list.setCurrentItem(item);
    else
        showDetails(-1);
    showResult();
}

QString OrCommandPicker::text() const {
    QVector<const Choice *> on;
    for (const Choice &choice : choices)
        if (choice.on)
            on << &choice;
    std::stable_sort(on.begin(), on.end(), [](const Choice *a, const Choice *b) { return a->order < b->order; });
    QStringList out;
    for (const Choice *choice : on)
        out << choice->text();
    return out.join(' ');
}

QIcon OrCommandPicker::Icon(const QWidget *widget) {
    const qreal ratio = widget != nullptr ? widget->devicePixelRatioF() : 1.0;
    QPixmap pixmap(QSize(16, 16) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(widget != nullptr ? widget->palette().color(QPalette::Text) : QColor(Qt::black));
    for (int i = 0; i < 3; ++i)
        painter.drawEllipse(QPointF(3.5 + i * 4.5, 8.5), 1.25, 1.25);
    return QIcon(pixmap);
}

QTreeWidgetItem *OrCommandPicker::itemOf(int choice) {
    for (int h = 0; h < list.topLevelItemCount(); ++h)
        for (int i = 0; i < list.topLevelItem(h)->childCount(); ++i) {
            QTreeWidgetItem *item = list.topLevelItem(h)->child(i);
            if (item->data(0, ChoiceRole).toInt() == choice)
                return item;
        }
    return nullptr;
}

void OrCommandPicker::setOn(int k, bool on) {
    if (k < 0 || k >= choices.size())
        return;
    Choice &choice = choices[k];
    choice.on = on;
    if (on && choice.order < 0)
        choice.order = nextOrder++;
    if (QTreeWidgetItem *item = itemOf(k)) {
        QSignalBlocker blocker(list);
        item->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
    }
    showResult();
}

void OrCommandPicker::showResult() {
    result.setText(text());
    result.setCursorPosition(0);
}

QWidget *OrCommandPicker::valueField(OrCommandSpec::Value kind, const QString &hint, const QString &value,
                                     const std::function<void(const QString &)> &changed) {
    if (kind == OrCommandSpec::Train || kind == OrCommandSpec::Path) {
        QComboBox *box = new QComboBox;
        box->setEditable(true);
        box->setInsertPolicy(QComboBox::NoInsert);
        box->setStyleSheet("combobox-popup: 0;");
        box->setMaxVisibleItems(20);
        box->addItems(kind == OrCommandSpec::Train ? trainNames : pathNames);
        box->setCurrentIndex(-1);
        box->setEditText(value);
        box->lineEdit()->setPlaceholderText(hint);
        QObject::connect(box, &QComboBox::editTextChanged, this, [changed](const QString &text) { changed(text); });
        // Short: the room left for what it does (the list shows a name
        // whole).
        box->setFixedWidth(170);
        box->view()->setMinimumWidth(240);
        return box;
    }
    QLineEdit *edit = new QLineEdit(value);
    edit->setFixedWidth(170);
    edit->setPlaceholderText(kind == OrCommandSpec::Speed ? speedUnit : hint);
    QObject::connect(edit, &QLineEdit::textEdited, this, [changed](const QString &text) { changed(text); });
    return edit;
}

void OrCommandPicker::showDetails(int k) {
    QWidget *page = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 2, 2, 2);
    layout->setSpacing(4);
    if (k < 0 || k >= choices.size()) {
        layout->addStretch(1);
        detailsArea.setWidget(page);
        return;
    }
    const Choice &choice = choices[k];
    QLabel *name = new QLabel(choice.spec != nullptr ? "$" + choice.spec->name : "$" + choice.typed);
    QFont big = name->font();
    big.setBold(true);
    big.setPointSizeF(big.pointSizeF() * 1.2);
    name->setFont(big);
    name->setWordWrap(true);
    layout->addWidget(name);
    if (choice.spec == nullptr) {
        QLabel *about = new QLabel(
            //% "Open Rails does not take this command here, or it is written a second time: it is kept as typed. Change it here, or untick it to leave it out."
            qtTrId("route.editor.activity.or.commands.typed.about"));
        about->setWordWrap(true);
        layout->addWidget(about);
        QLineEdit *typed = new QLineEdit(choice.typed);
        typed->setObjectName("orCommandTyped");
        QObject::connect(typed, &QLineEdit::textEdited, this, [this, k](const QString &text) {
            choices[k].typed = text.trimmed();
            setOn(k, !choices[k].typed.isEmpty());
        });
        layout->addWidget(typed);
        layout->addStretch(1);
        detailsArea.setWidget(page);
        return;
    }
    const OrCommandSpec &spec = *choice.spec;
    QLabel *about = new QLabel(spec.description);
    about->setWordWrap(true);
    layout->addWidget(about);
    if (!OrTimetableCommandRun(spec.name)) {
        QLabel *notRun = new QLabel(notRunText());
        notRun->setWordWrap(true);
        notRun->setStyleSheet(QString("QLabel { color: %1; }").arg(NotRunColour.name()));
        layout->addWidget(notRun);
    }
    QGridLayout *grid = new QGridLayout;
    // A row each: the box, its value, what it does.
    grid->setContentsMargins(0, 6, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(3);
    grid->setColumnStretch(2, 1);
    int row = 0;
    if (spec.value != OrCommandSpec::None) {
        grid->addWidget(new QLabel(
            //% "Value:"
            qtTrId("route.editor.activity.or.commands.value")), row, 0);
        QWidget *field = valueField(spec.value, spec.hint, choice.value, [this, k](const QString &text) {
            choices[k].value = text;
            setOn(k, true);
        });
        field->setObjectName("orCommandValue");
        grid->addWidget(field, row, 1);
        if (spec.valueOptional) {
            QLabel *optional = new QLabel(
                //% "May be left out."
                qtTrId("route.editor.activity.or.commands.value.optional"));
            optional->setForegroundRole(QPalette::PlaceholderText);
            grid->addWidget(optional, row, 2);
        }
        ++row;
    }
    if (!spec.qualifiers.isEmpty()) {
        grid->addItem(new QSpacerItem(0, 6), row++, 0);
        //% "Qualifiers:"
        grid->addWidget(GuiFunct::newTQLabel(qtTrId("route.editor.activity.or.commands.qualifiers")), row++, 0, 1, 3);
    }
    for (const OrCommandSpec::QualifierSpec &qualifierSpec : spec.qualifiers) {
        const QString qualifier = qualifierSpec.name;
        int at = -1;
        for (int i = 0; i < choice.qualifiers.size() && at < 0; ++i)
            if (choice.qualifiers[i].name == qualifier)
                at = i;
        QCheckBox *box = new QCheckBox("/" + qualifier);
        box->setObjectName("orCommandQualifier." + qualifier);
        box->setChecked(at >= 0);
        if (!OrTimetableQualifierRun(spec.name, qualifier) && OrTimetableCommandRun(spec.name)) {
            box->setStyleSheet(QString("QCheckBox { color: %1; }").arg(NotRunColour.name()));
            box->setToolTip(notRunText());
        }
        grid->addWidget(box, row, 0);
        QWidget *field = nullptr;
        if (qualifierSpec.value != OrCommandSpec::None) {
            field = valueField(qualifierSpec.value, qualifierSpec.hint, at >= 0 ? choice.qualifiers[at].value : QString(),
                               [this, k, qualifier, box](const QString &text) {
                Choice &chosen = choices[k];
                bool found = false;
                for (Qualifier &written : chosen.qualifiers)
                    if (written.name == qualifier) {
                        written.value = text;
                        found = true;
                    }
                if (!found)
                    chosen.qualifiers << Qualifier{qualifier, text};
                QSignalBlocker blocker(box);
                box->setChecked(true);
                setOn(k, true);
            });
            field->setObjectName("orCommandQualifierValue." + qualifier);
            grid->addWidget(field, row, 1);
        }
        QLabel *what = new QLabel(qualifierSpec.description);
        what->setWordWrap(true);
        what->setForegroundRole(QPalette::PlaceholderText);
        grid->addWidget(what, row++, 2);
        QObject::connect(box, &QCheckBox::toggled, this, [this, k, qualifier, field](bool checked) {
            Choice &chosen = choices[k];
            for (int i = chosen.qualifiers.size() - 1; i >= 0; --i)
                if (chosen.qualifiers[i].name == qualifier)
                    chosen.qualifiers.remove(i);
            if (checked) {
                QString value;
                if (QLineEdit *edit = qobject_cast<QLineEdit *>(field))
                    value = edit->text();
                else if (QComboBox *combo = qobject_cast<QComboBox *>(field))
                    value = combo->currentText();
                chosen.qualifiers << Qualifier{qualifier, value};
                setOn(k, true);
            } else {
                showResult();
            }
        });
    }
    // Written qualifiers it does not know: kept.
    QStringList unknown;
    for (const Qualifier &written : choice.qualifiers)
        if (spec.qualifier(written.name) == nullptr)
            unknown << "/" + written.name + (written.value.isEmpty() ? QString() : "=" + written.value);
    if (!unknown.isEmpty()) {
        QLabel *kept = new QLabel(
            //% "Kept as typed: %1"
            qtTrId("route.editor.activity.or.commands.qualifiers.typed").arg(unknown.join(' ')));
        kept->setWordWrap(true);
        grid->addWidget(kept, row++, 0, 1, 3);
    }
    layout->addLayout(grid);
    layout->addStretch(1);
    detailsArea.setWidget(page);
}
