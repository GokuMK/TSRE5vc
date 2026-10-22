#include <routeEditor/activity/ActivityEdit.h>
#include <tsre/Undo.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/sim/EventResults.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/Service.h>

ActivityEdit::ActivityEdit(Activity *activity, bool typing) : typing(typing) {
    begin();
    Undo::PushActivityData(activity);
    // Its last run's event results are from before (task simulation 06).
    Sim::EventResultStore::instance()->outdate(activity);
}

ActivityEdit::ActivityEdit(Service *service, bool typing) : typing(typing) {
    begin();
    Undo::PushServiceData(service);
}

ActivityEdit::ActivityEdit(Traffic *traffic, bool typing) : typing(typing) {
    begin();
    Undo::PushTrafficData(traffic);
}

void ActivityEdit::begin() {
    if (typing)
        Undo::StateBeginIfNotExist();
    else
        Undo::StateBegin();
}

void ActivityEdit::also(Activity *activity) {
    Undo::PushActivityData(activity);
    Sim::EventResultStore::instance()->outdate(activity);
}

void ActivityEdit::also(Service *service) {
    Undo::PushServiceData(service);
}

void ActivityEdit::also(Traffic *traffic) {
    Undo::PushTrafficData(traffic);
}

void ActivityEdit::alsoUsersOf(Service *service) {
    if (service == nullptr)
        return;
    for (Activity *activity : ActLib::Act)
        if (activity != nullptr && activity->loaded == 1 && activity->routeHashid == service->routeHashid
                && (activity->isServiceInUse(service->nameId) || activity->isPlayerServiceInUse(service->nameId)))
            Undo::PushActivityData(activity);
}

void ActivityEdit::alsoTrafficOf(Activity *activity) {
    if (activity == nullptr || activity->traffic == nullptr)
        return;
    Undo::PushTrafficData(ActLib::GetTrafficByName(activity->traffic->name,
                                                   ContentPath::parentDirectory(activity->path)));
}

void ActivityEdit::cancel() {
    if (typing || cancelled)
        return;
    Undo::StateCancel();
    cancelled = true;
}

ActivityEdit::~ActivityEdit() {
    if (!typing && !cancelled)
        Undo::StateEnd();
}
