#include <tsre/trains/ActivityOverlay.h>

#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityEvent.h>

namespace {

using Outcome = ActivityEvent::Outcome;

QString text(Outcome *outcome) {
    QString written;
    QTextStream out(&written);
    outcome->save(&out);
    out.flush();
    return written.simplified();
}

// Open Rails keeps one of these per event; the others are lists.
bool single(Outcome::OutcomeType type) {
    return type == Outcome::TypeDisplayMessage || type == Outcome::TypeActivitySuccess
        || type == Outcome::TypeActivityFail || type == Outcome::TypeORTSActSoundFile
        || type == Outcome::TypeORTSWeatherChange || type == Outcome::TypeORTSRestartWaitingTrain;
}

QString outcomeName(const Outcome *outcome) {
    return Outcome::OutcomeTypeName.value(outcome->type);
}

}

QString ActivityOverlay::PathOf(const Activity *activity) {
    if (activity == nullptr)
        return {};
    // The activity's file name as spelled in the fixed OPENRAILS folder
    // (content in another letter case is for the content-case converter).
    const QString path = activity->path + "/OPENRAILS/" + activity->name;
    return QFileInfo(path).isFile() ? path : QString();
}

ActivityOverlay::Result ActivityOverlay::Merge(Activity *activity, const QString &overlayPath, bool apply) {
    Result result;
    const QFileInfo info(overlayPath);
    Activity overlay(overlayPath, info.absolutePath(), info.fileName());
    if (activity == nullptr || overlay.loaded != 1) {
        result.readable = false;
        return result;
    }
    if (overlay.ortsAIHornAtCrossings != -9999) {
        const bool on = overlay.ortsAIHornAtCrossings > 0;
        if (activity->ortsAIHornAtCrossings != -9999 && activity->isOrtsHornAtCrossigns() == on)
            //% "AI horn at crossings: the activity has it already."
            result.skipped << qtTrId("activity.overlay.horn.same");
        else {
            //% "AI horn at crossings: %1."
            result.added << qtTrId("activity.overlay.horn").arg(on ? "1" : "0");
            if (apply)
                activity->setOrtsHornAtCrossigns(on);
        }
    }
    if (!overlay.ortsAICrossingHornPattern.isEmpty()) {
        if (activity->ortsAICrossingHornPattern.compare(overlay.ortsAICrossingHornPattern, Qt::CaseInsensitive) == 0)
            //% "AI horn pattern: the activity has it already."
            result.skipped << qtTrId("activity.overlay.pattern.same");
        else {
            //% "AI horn pattern: %1."
            result.added << qtTrId("activity.overlay.pattern").arg(overlay.ortsAICrossingHornPattern);
            if (apply)
                activity->setOrtsHornPattern(overlay.ortsAICrossingHornPattern);
        }
    }
    for (ActivityEvent *from : overlay.event) {
        if (from == nullptr)
            continue;
        ActivityEvent *to = nullptr;
        for (ActivityEvent *event : activity->event)
            if (event != nullptr && event->id == from->id)
                to = event;
        if (to == nullptr) {
            //% "Event %1: not in the activity."
            result.skipped << qtTrId("activity.overlay.event.missing").arg(from->id);
            continue;
        }
        if (to->category != from->category) {
            //% "Event %1: of another kind in the activity."
            result.skipped << qtTrId("activity.overlay.event.category").arg(from->id);
            continue;
        }
        const QString event = QString("%1 %2").arg(to->id).arg(to->name);
        bool changed = false;
        const auto field = [&](bool given, bool same, const QString &what, const std::function<void()> &set) {
            if (!given)
                return;
            if (same) {
                //% "Event %1: %2, the activity has it already."
                result.skipped << qtTrId("activity.overlay.event.same").arg(event, what);
                return;
            }
            //% "Event %1: %2."
            result.added << qtTrId("activity.overlay.event.field").arg(event, what);
            if (apply) {
                set();
                changed = true;
            }
        };
        field(from->ortsContinue != -99999, to->ortsContinue == from->ortsContinue,
              QString("ORTSContinue %1").arg(from->ortsContinue), [&] { to->ortsContinue = from->ortsContinue; });
        field(!from->triggeringService.isEmpty(), to->triggeringService == from->triggeringService
              && to->triggeringTime == from->triggeringTime,
              QString("ORTSTriggeringTrain %1").arg(from->triggeringService), [&] {
                  to->triggeringService = from->triggeringService;
                  to->triggeringTime = from->triggeringTime;
              });
        field(from->reversableEvent, to->reversableEvent, "Reversible_Event", [&] {
            to->reversableEvent = true;
            to->reversibleSpelling = from->reversibleSpelling;
        });
        field(from->activationLevel != -99999, to->activationLevel == from->activationLevel,
              QString("Activation_Level %1").arg(from->activationLevel), [&] { to->activationLevel = from->activationLevel; });
        field(!from->name.isEmpty(), to->name == from->name, QString("Name %1").arg(from->name),
              [&] { to->name = from->name; });
        field(!from->textToDisplayOnCompletionIfTriggered.isEmpty(),
              to->textToDisplayOnCompletionIfTriggered == from->textToDisplayOnCompletionIfTriggered,
              "TextToDisplayOnCompletionIfTriggered",
              [&] { to->textToDisplayOnCompletionIfTriggered = from->textToDisplayOnCompletionIfTriggered; });
        field(!from->textToDisplayOnCompletionIfNotTriggered.isEmpty(),
              to->textToDisplayOnCompletionIfNotTriggered == from->textToDisplayOnCompletionIfNotTriggered,
              "TextToDisplayOnCompletionIfNotTriggered",
              [&] { to->textToDisplayOnCompletionIfNotTriggered = from->textToDisplayOnCompletionIfNotTriggered; });
        field(from->triggerOnStop != -99999, to->triggerOnStop == from->triggerOnStop,
              QString("TriggerOnStop %1").arg(from->triggerOnStop), [&] { to->triggerOnStop = from->triggerOnStop; });
        field(from->time != -99999, to->time == from->time, QString("Time %1").arg(from->time),
              [&] { to->time = from->time; });
        field(from->ortsActSoundFileDeprecated.size() >= 2, to->ortsActSoundFileDeprecated == from->ortsActSoundFileDeprecated,
              QString("ORTSActSoundFile %1").arg(from->ortsActSoundFileDeprecated.value(0)),
              [&] { to->ortsActSoundFileDeprecated = from->ortsActSoundFileDeprecated; });
        for (Outcome *outcome : from->outcomes) {
            bool same = false;
            int replaced = -1;
            for (int i = 0; i < to->outcomes.size(); ++i) {
                if (to->outcomes[i]->type != outcome->type)
                    continue;
                if (text(to->outcomes[i]) == text(outcome))
                    same = true;
                else if (single(outcome->type))
                    replaced = i;
            }
            field(true, same, outcomeName(outcome) + (replaced >= 0 ?
                  //% "(replacing the activity's)"
                  " " + qtTrId("activity.overlay.replacing") : QString()), [&] {
                Outcome *copy = new Outcome(*outcome);
                copy->setModified(true);
                if (replaced >= 0)
                    to->outcomes[replaced] = copy;
                else
                    to->outcomes.push_back(copy);
            });
        }
        if (apply && changed)
            to->setModified(true);
    }
    return result;
}
