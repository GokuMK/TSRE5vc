#include <routeEditor/activity/ActivityCheckWindow.h>

#include <QBrush>
#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTime>
#include <algorithm>
#include <cmath>
#include <tsre/sim/EventResults.h>
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
    appendLastRun();
    std::stable_sort(findings.begin(), findings.end(), [](const ActivityFinding &a, const ActivityFinding &b) {
        return a.severity < b.severity;
    });
    int errors = 0, warnings = 0;
    for (int i = 0; i < findings.size(); ++i) {
        const ActivityFinding::Severity severity = findings[i].severity;
        errors += severity == ActivityFinding::Error ? 1 : 0;
        warnings += severity == ActivityFinding::Warning ? 1 : 0;
        QString kind;
        QColor colour;
        if (severity == ActivityFinding::Error) {
            //% "Error"
            kind = qtTrId("route.editor.activity.check.window.error");
            colour = QColor(200, 40, 40);
        } else if (severity == ActivityFinding::Warning) {
            //% "Warning"
            kind = qtTrId("route.editor.activity.check.window.warning");
            colour = QColor(200, 130, 0);
        } else {
            //% "Last run"
            kind = qtTrId("route.editor.activity.check.window.run");
            colour = QColor(40, 110, 200);
        }
        QTreeWidgetItem *item = new QTreeWidgetItem(&list, {kind, findings[i].text}, i);
        item->setForeground(0, QBrush(colour));
        item->setToolTip(1, findings[i].text);
    }
    if (activity == nullptr)
        summary.clear();
    else if (errors + warnings == 0)
        summary.setText(
            //% "%1: no problems found."
            qtTrId("route.editor.activity.check.window.none").arg(activity->header ? activity->header->name : activity->name));
    else
        summary.setText(
            //% "%1: %2 errors, %3 warnings."
            qtTrId("route.editor.activity.check.window.summary")
            .arg(activity->header ? activity->header->name : activity->name).arg(errors).arg(warnings));
}

void ActivityCheckWindow::appendLastRun() {
    bool current = true;
    const Sim::EventResults *results = activity != nullptr ? Sim::EventResultStore::instance()->of(activity, &current) : nullptr;
    if (results == nullptr)
        return;
    const auto timeOfDay = [](double seconds) {
        return QTime(0, 0).addSecs(int(std::floor(seconds)) % 86400).toString("HH:mm:ss");
    };
    const auto add = [this](const QString &text, int index) {
        ActivityFinding finding;
        finding.severity = ActivityFinding::Run;
        finding.code = "run";
        finding.text = text;
        if (index >= 0 && index < activity->event.size() && activity->event[index] != nullptr) {
            finding.target = ActivityFinding::Event;
            finding.eventId = activity->event[index]->id;
        }
        findings.push_back(finding);
    };
    bool any = false;
    for (const Sim::EventResult &result : results->events)
        any = any || result.inRun;
    if (!any && results->warnings.isEmpty())
        return;
    if (!current)
        //% "The last run is from before the activity was edited: simulate it again."
        add(qtTrId("route.editor.activity.check.run.outdated"), -1);
    // What the run's input noted on the trains it made (not simulated,
    // stops not found, driven simply), an Open Rails timetable set's too
    // (it has no events).
    for (const QString &warning : results->warnings)
        add(warning, -1);
    if (!any)
        return;
    if (results->end == Sim::ActivityEnd::Success)
        //% "The activity ends successfully at %1."
        add(qtTrId("route.editor.activity.check.run.success").arg(timeOfDay(results->endedAt)), -1);
    else if (results->end == Sim::ActivityEnd::Failure)
        //% "The activity ends without success at %1."
        add(qtTrId("route.editor.activity.check.run.failure").arg(timeOfDay(results->endedAt)), -1);
    else if (results->complete)
        //% "The activity does not end: no event ending it fired."
        add(qtTrId("route.editor.activity.check.run.no.end"), -1);
    else
        //% "The last run went up to %1 only."
        add(qtTrId("route.editor.activity.check.run.up.to").arg(timeOfDay(results->time)), -1);
    for (size_t i = 0; i < results->events.size(); ++i) {
        const Sim::EventResult &result = results->events[i];
        if (!result.inRun)
            continue;
        if (result.state.fired == 0)
            //% "Event %1 did not fire. %2"
            add(qtTrId("route.editor.activity.check.run.not.fired").arg(result.name, result.why), int(i));
        else if (result.state.standIn)
            //% "Event %1 was fired by the player's train: its triggering train had not started (Open Rails does so)."
            add(qtTrId("route.editor.activity.check.run.standin").arg(result.name), int(i));
    }
    // What MSTS shows at the activity's end, for each event as it went.
    if (results->end != Sim::ActivityEnd::None || results->complete)
        for (size_t i = 0; i < results->events.size(); ++i)
            if (results->events[i].inRun && !results->events[i].completion.isEmpty())
                //% "At the end, event %1 shows: %2"
                add(qtTrId("route.editor.activity.check.run.completion")
                        .arg(results->events[i].name, results->events[i].completion.simplified()), int(i));
}
