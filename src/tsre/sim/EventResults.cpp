#include <tsre/sim/EventResults.h>

#include <tsre/sim/SimulationTexts.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityNotifier.h>

namespace Sim {

EventResultStore *EventResultStore::instance() {
    static EventResultStore *store = new EventResultStore();
    return store;
}

EventResultStore::EventResultStore() {
    // Undo replaced activity data: every activity's results may be from
    // before.
    connect(ActivityNotifier::instance(), &ActivityNotifier::restored, this, [this] {
        for (auto &[activity, entry] : entries)
            if (entry.current) {
                entry.current = false;
                ++changes;
                emit changed(activity);
            }
    });
}

void EventResultStore::record(Activity *activity, const SimulationInput &input, const std::vector<EventState> &states,
                              ActivityEnd end, double endedAt, double time, bool complete) {
    if (activity == nullptr)
        return;
    Entry &entry = entries[activity];
    EventResults &results = entry.results;
    results.events.assign(size_t(std::max<qsizetype>(0, activity->event.size())), EventResult());
    for (size_t e = 0; e < input.events.size() && e < states.size(); ++e) {
        const int index = input.events[e].index;
        if (index < 0 || size_t(index) >= results.events.size())
            continue;
        EventResult &result = results.events[size_t(index)];
        result.inRun = true;
        result.state = states[e];
        result.by = states[e].firedBy >= 0 ? TrainName(input, states[e].firedBy) : QString();
        result.why = states[e].fired > 0 ? QString() : WhyNotFired(input, int(e), states[e], end, endedAt);
        if (const ActivityEvent *event = activity->event[index]) {
            result.name = event->name;
            result.completion = states[e].fired > 0 ? event->textToDisplayOnCompletionIfTriggered
                                                    : event->textToDisplayOnCompletionIfNotTriggered;
        }
    }
    results.time = time;
    results.complete = complete;
    results.end = end;
    results.endedAt = endedAt;
    entry.current = true;
    ++changes;
    emit changed(activity);
}

void EventResultStore::advance(const Activity *activity, double time, bool complete) {
    auto found = entries.find(activity);
    if (found == entries.end())
        return;
    found->second.results.time = time;
    found->second.results.complete = complete;
}

void EventResultStore::outdate(const Activity *activity) {
    auto found = entries.find(activity);
    if (found == entries.end() || !found->second.current)
        return;
    found->second.current = false;
    ++changes;
    emit changed(activity);
}

const EventResults *EventResultStore::of(const Activity *activity, bool *current) const {
    auto found = entries.find(activity);
    if (found == entries.end())
        return nullptr;
    if (current != nullptr)
        *current = found->second.current;
    return &found->second.results;
}

void EventResultStore::forgetAll() {
    entries.clear();
    ++changes;
}

}
