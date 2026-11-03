#pragma once
#include <QDialog>
#include <QtWidgets>

// The tags picker of the Timetable window's cars (task scripting 03): the
// recommended tags by group (class and type, UIC letters; features), each
// with what it means, and the user's own (the car's, the timetable's other
// cars', added here); ticked, they make the car's tags, the recommended
// first in their order, then the own in theirs (typed in a cell, tags keep
// the order typed). Opened by the "…" of the cars' Tags cells; OK gives
// text() to the cell.
class OrTagsPicker : public QDialog {
    Q_OBJECT
public:
    enum Group { ClassGroup, FeatureGroup };
    struct Recommended {
        QString tag;
        Group group;
        QString description;
    };
    // The recommended tags in their order (TSRE's for now; the user,
    // 2026-11-03: perhaps a route's list later).
    static const QVector<Recommended> &RecommendedTags();
    static bool IsRecommended(const QString &tag);

    // `title` the car's ("24 · PKPIC_B9mnopuz-90013"), `tags` its cell's,
    // `own` the timetable's other tags not recommended, offered.
    OrTagsPicker(const QString &title, const QString &tags, const QStringList &own, QWidget *parent = nullptr);
    // The tags as chosen, as a cell takes them ("B bike vip").
    QString text() const;

private:
    QTreeWidget list;
    QTreeWidgetItem *ownHead = nullptr;
    QLineEdit added;
    QLineEdit result;
    // A tag's item, none when not listed.
    QTreeWidgetItem *itemOf(const QString &tag) const;
    QTreeWidgetItem *addOwn(const QString &tag, bool on);
    // The tag typed: ticked, listed among the own first if not there.
    void addTyped();
    void showResult();
};
