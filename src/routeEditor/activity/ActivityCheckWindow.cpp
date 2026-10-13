#include <routeEditor/activity/ActivityCheckWindow.h>

#include <QBrush>
#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <tsre/trains/Activity.h>

ActivityCheckWindow::ActivityCheckWindow(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::WindowType::Tool);
    setWindowTitle(
        //% "Check Activity"
        qtTrId("route.editor.activity.check.window.title"));
    list.setColumnCount(2);
    list.setHeaderLabels({
        //% "Kind"
        qtTrId("route.editor.activity.check.window.column.kind"),
        //% "Problem"
        qtTrId("route.editor.activity.check.window.column.problem")});
    list.setRootIsDecorated(false);
    list.setWordWrap(true);
    list.header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    list.setMinimumSize(520, 260);
    QPushButton *again = new QPushButton(
        //% "Check Again"
        qtTrId("route.editor.activity.check.window.button.again"));
    QVBoxLayout *layout = new QVBoxLayout;
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addWidget(&summary);
    layout->addWidget(&list);
    layout->addWidget(again);
    setLayout(layout);
    QObject::connect(again, &QPushButton::released, this, [this] { check(activity); });
    QObject::connect(&list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        const int index = item != nullptr ? item->type() : -1;
        if (index >= 0 && index < findings.size())
            emit findingChosen(findings[index]);
    });
}

void ActivityCheckWindow::check(Activity *checked) {
    activity = checked;
    list.clear();
    findings = CheckActivity(activity);
    std::stable_sort(findings.begin(), findings.end(), [](const ActivityFinding &a, const ActivityFinding &b) {
        return a.severity < b.severity;
    });
    int errors = 0;
    for (int i = 0; i < findings.size(); ++i) {
        const bool error = findings[i].severity == ActivityFinding::Error;
        errors += error ? 1 : 0;
        QTreeWidgetItem *item = new QTreeWidgetItem(&list, {
            error ?
                //% "Error"
                qtTrId("route.editor.activity.check.window.error") :
                //% "Warning"
                qtTrId("route.editor.activity.check.window.warning"),
            findings[i].text}, i);
        item->setForeground(0, QBrush(error ? QColor(200, 40, 40) : QColor(200, 130, 0)));
        item->setToolTip(1, findings[i].text);
    }
    if (activity == nullptr)
        summary.clear();
    else if (findings.isEmpty())
        summary.setText(
            //% "%1: no problems found."
            qtTrId("route.editor.activity.check.window.none").arg(activity->header ? activity->header->name : activity->name));
    else
        summary.setText(
            //% "%1: %2 errors, %3 warnings."
            qtTrId("route.editor.activity.check.window.summary")
            .arg(activity->header ? activity->header->name : activity->name).arg(errors).arg(findings.size() - errors));
}
