#include <tsre/trains/OrTimetableCommands.h>
#include <QCoreApplication>

namespace {

using Spec = OrCommandSpec;
using Qualifier = OrCommandSpec::QualifierSpec;

Spec command(const QString &name, Spec::Group group, int places, Spec::Value value, const QString &hint,
             const QString &description, const QVector<Qualifier> &qualifiers = {}) {
    Spec spec;
    spec.name = name;
    spec.group = group;
    spec.places = places;
    spec.value = value;
    spec.hint = hint;
    spec.description = description;
    spec.qualifiers = qualifiers;
    return spec;
}

QVector<Spec> made() {
    //% "HH:mm"
    const QString time = qtTrId("timetable.command.hint.time");
    //% "train"
    const QString train = qtTrId("timetable.command.hint.train");
    //% "minutes"
    const QString minutes = qtTrId("timetable.command.hint.minutes");
    //% "seconds"
    const QString seconds = qtTrId("timetable.command.hint.seconds");
    //% "metres"
    const QString metres = qtTrId("timetable.command.hint.metres");
    //% "factor"
    const QString factor = qtTrId("timetable.command.hint.factor");
    //% "percent"
    const QString percent = qtTrId("timetable.command.hint.percent");
    //% "path"
    const QString path = qtTrId("timetable.command.hint.path");
    //% "pool"
    const QString pool = qtTrId("timetable.command.hint.pool");
    //% "units (negative: from the rear)"
    const QString units = qtTrId("timetable.command.hint.units");
    //% "consist+consist"
    const QString consists = qtTrId("timetable.command.hint.consists");
    //% "forward or backward"
    const QString direction = qtTrId("timetable.command.hint.direction");

    const int stop = Spec::Stop | Spec::Station;
    const int timing = Spec::Stop | Spec::Note;
    const int shunting = Spec::Stop | Spec::Note | Spec::Dispose;
    // Shared by $wait and $follow.
    const QVector<Qualifier> waits = {
        {"maxdelay", Spec::Number, minutes,
         //% "Not when the other train is later than this, less this train's own delay."
         qtTrId("timetable.command.wait.maxdelay")},
        {"notstarted", Spec::None, QString(),
         //% "Also when the other train has not started yet."
         qtTrId("timetable.command.wait.notstarted")},
        {"owndelay", Spec::Number, minutes,
         //% "Only when this train is at least this late."
         qtTrId("timetable.command.wait.owndelay")},
        {"trigger", Spec::Time, time,
         //% "Only after this time (experimental in Open Rails)."
         qtTrId("timetable.command.wait.trigger")},
        {"endtrigger", Spec::Time, time,
         //% "Only before this time (experimental in Open Rails)."
         qtTrId("timetable.command.wait.endtrigger")}};
    QVector<Qualifier> waitQualifiers = waits;
    waitQualifiers.insert(2, {"atstart", Spec::None, QString(),
                              //% "Waits where it is, not where the routes join."
                              qtTrId("timetable.command.wait.atstart")});
    // Shared by $forms and $triggers.
    const QVector<Qualifier> forms = {
        {"runround", Spec::Path, path,
         //% "Its engine runs round the train by this path."
         qtTrId("timetable.command.forms.runround")},
        {"rrtime", Spec::Time, time,
         //% "When the run-round takes place (with /runround)."
         qtTrId("timetable.command.forms.rrtime")},
        {"setstop", Spec::None, QString(),
         //% "Without stops of its own: the new train's first stop taken as its end."
         qtTrId("timetable.command.forms.setstop")},
        {"atstation", Spec::None, QString(),
         //% "Ends where it would stop at the station."
         qtTrId("timetable.command.forms.atstation")},
        {"closeup", Spec::None, QString(),
         //% "Ends close up to the end of the track."
         qtTrId("timetable.command.forms.closeup")},
        {"speed", Spec::Speed, QString(),
         //% "The speed of the run-round (with /runround)."
         qtTrId("timetable.command.forms.speed")}};

    QVector<Spec> out;
    // #start
    out << command("create", Spec::StartGroup, Spec::Start, Spec::Time, time,
                   //% "Puts the train on its path at this time (none: before the first train starts); it stands there until its start time."
                   qtTrId("timetable.command.create"),
                   {{"ahead", Spec::Train, train,
                     //% "Placed ahead of that train's front, in the direction of its path (trains stored on one siding)."
                     qtTrId("timetable.command.create.ahead")}});
    out.last().valueOptional = true;
    out << command("next", Spec::StartGroup, Spec::Start, Spec::None, QString(),
                   //% "Its start time is after midnight, at the end of the timetable's day."
                   qtTrId("timetable.command.next"));
    out << command("static", Spec::StartGroup, Spec::Start, Spec::None, QString(),
                   //% "A static train: it stands at its path's start and never moves."
                   qtTrId("timetable.command.static"),
                   {{"ahead", Spec::Train, train,
                     //% "Placed ahead of that train's front."
                     qtTrId("timetable.command.static.ahead")},
                    {"pool", Spec::Text, pool,
                     //% "Created in that pool."
                     qtTrId("timetable.command.static.pool")}});
    out << command("pool", Spec::StartGroup, Spec::Start, Spec::Text, pool,
                   //% "Starts out of that pool of stored trains."
                   qtTrId("timetable.command.pool"),
                   {{"direction", Spec::Text, direction,
                     //% "The way it leaves a turntable pool (none: it reverses)."
                     qtTrId("timetable.command.pool.direction")},
                    {"set_consist_name", Spec::None, QString(),
                     //% "The vehicles taken out of the pool get this train's consist name."
                     qtTrId("timetable.command.pool.set.consist.name")}});
    out << command("activated", Spec::StartGroup, Spec::Start, Spec::None, QString(),
                   //% "Starts only once another train activates it ($activate), at its start time or later."
                   qtTrId("timetable.command.activated"));
    // #note
    out << command("acc", Spec::NoteGroup, Spec::Note, Spec::Number, factor,
                   //% "Its acceleration: the default multiplied by this factor."
                   qtTrId("timetable.command.acc"));
    out << command("dec", Spec::NoteGroup, Spec::Note, Spec::Number, factor,
                   //% "Its deceleration and braking distances: the default multiplied by this factor (2 or 3 for modern stock)."
                   qtTrId("timetable.command.dec"));
    out << command("doo", Spec::NoteGroup, Spec::Note, Spec::None, QString(),
                   //% "Driver only operation: no departure sound at stations (the player's train)."
                   qtTrId("timetable.command.doo"));
    out << command("forcereversal", Spec::NoteGroup, Spec::Note, Spec::None, QString(),
                   //% "Reverses as soon as it is clear of the reversal point, not beyond the signal there (shunting in yards)."
                   qtTrId("timetable.command.forcereversal"));
    // The speed row
    out << command("max", Spec::SpeedGroup, Spec::SpeedRow, Spec::Speed, QString(),
                   //% "Its top speed."
                   qtTrId("timetable.command.max"));
    out << command("cruise", Spec::SpeedGroup, Spec::SpeedRow, Spec::Speed, QString(),
                   //% "The speed it runs at while on time; later than $maxdelay it runs at its top speed."
                   qtTrId("timetable.command.cruise"));
    out << command("maxdelay", Spec::SpeedGroup, Spec::SpeedRow, Spec::Number, minutes,
                   //% "How late it may be before it leaves its cruise speed."
                   qtTrId("timetable.command.maxdelay"));
    out << command("creep", Spec::SpeedGroup, Spec::SpeedRow, Spec::Speed, QString(),
                   //% "Its least speed on the last approach to a red signal or a stop."
                   qtTrId("timetable.command.creep"));
    out << command("attach", Spec::SpeedGroup, Spec::SpeedRow, Spec::Speed, QString(),
                   //% "Its speed when it attaches to another train."
                   qtTrId("timetable.command.speed.attach"));
    out << command("detach", Spec::SpeedGroup, Spec::SpeedRow, Spec::Speed, QString(),
                   //% "Its speed when it detaches from another train."
                   qtTrId("timetable.command.speed.detach"));
    out << command("movingtable", Spec::SpeedGroup, Spec::SpeedRow, Spec::Speed, QString(),
                   //% "Its speed on turntables."
                   qtTrId("timetable.command.movingtable"));
    out << command("gradient", Spec::SpeedGroup, Spec::SpeedRow, Spec::None, QString(),
                   //% "Forced acceleration off on rising gradients steeper than /perc, on again below /speed so it does not stall."
                   qtTrId("timetable.command.gradient"),
                   {{"perc", Spec::Number, percent,
                     //% "The gradient."
                     qtTrId("timetable.command.gradient.perc")},
                    {"speed", Spec::Speed, QString(),
                     //% "The speed below which it is forced on again."
                     qtTrId("timetable.command.gradient.speed")}});
    // A station's or a stop's
    out << command("hold", Spec::StationGroup, stop, Spec::None, QString(),
                   //% "The exit signal held at danger until 2 minutes before the departure."
                   qtTrId("timetable.command.hold"));
    out << command("nohold", Spec::StationGroup, stop, Spec::None, QString(),
                   //% "The exit signal not held."
                   qtTrId("timetable.command.nohold"));
    out << command("forcehold", Spec::StationGroup, stop, Spec::None, QString(),
                   //% "The first signal beyond the platform held as the exit signal, even where it is not the platform's."
                   qtTrId("timetable.command.forcehold"));
    out << command("forcewait", Spec::StationGroup, stop, Spec::None, QString(),
                   //% "Waits at a red next signal even where it is not the platform's exit signal."
                   qtTrId("timetable.command.forcewait"));
    out << command("nowaitsignal", Spec::StationGroup, stop, Spec::None, QString(),
                   //% "Leaves without waiting for the next signal to clear (halts without signals, freight trains)."
                   qtTrId("timetable.command.nowaitsignal"));
    out << command("waitsignal", Spec::StationGroup, Spec::Stop | Spec::Note, Spec::None, QString(),
                   //% "Waits for the next signal to clear, against the station's $nowaitsignal."
                   qtTrId("timetable.command.waitsignal"));
    out << command("terminal", Spec::StationGroup, stop, Spec::None, QString(),
                   //% "Stops at the platform's terminating end where its path starts or ends there."
                   qtTrId("timetable.command.terminal"));
    out << command("closeupsignal", Spec::StationGroup, stop, Spec::None, QString(),
                   //% "Stops closer to the signal ahead, using more of the platform."
                   qtTrId("timetable.command.closeupsignal"));
    out << command("extendplatformtosignal", Spec::StationGroup, stop, Spec::None, QString(),
                   //% "The platform's end taken just short of the signal beyond its marker."
                   qtTrId("timetable.command.extendplatformtosignal"));
    out << command("restrictplatformtosignal", Spec::StationGroup, stop, Spec::None, QString(),
                   //% "The platform's end taken just short of its exit signal where the marker is beyond it."
                   qtTrId("timetable.command.restrictplatformtosignal"));
    out << command("stoptime", Spec::StationGroup, stop, Spec::Number, seconds,
                   //% "The least time standing there, in place of the route's."
                   qtTrId("timetable.command.stoptime"));
    out << command("keepclear", Spec::StationGroup, Spec::Stop, Spec::None, QString(),
                   //% "Stops so that part of the platform stays clear ahead of or behind the train (for a train to attach or to come in)."
                   qtTrId("timetable.command.keepclear"),
                   {{"front", Spec::Number, metres,
                     //% "Clear ahead of the train."
                     qtTrId("timetable.command.keepclear.front")},
                    {"rear", Spec::Number, metres,
                     //% "Clear behind the train."
                     qtTrId("timetable.command.keepclear.rear")},
                    {"force", Spec::None, QString(),
                     //% "Kept clear even past the exit signal or with the rear outside the platform."
                     qtTrId("timetable.command.keepclear.force")}});
    out << command("endstop", Spec::StationGroup, Spec::Stop, Spec::None, QString(),
                   //% "Its path ends at this stop even where it goes on."
                   qtTrId("timetable.command.endstop"));
    out << command("closeup", Spec::StationGroup, Spec::Stop, Spec::None, QString(),
                   //% "Stops close to a train already in the platform (with $callon)."
                   qtTrId("timetable.command.closeup"));
    out << command("req", Spec::StationGroup, stop, Spec::None, QString(),
                   //% "A request stop's details (an x in its time): how likely someone gets on or off."
                   qtTrId("timetable.command.req"),
                   {{"pu", Spec::Number, percent,
                     //% "The chance someone gets on (0 to 99, 25 by default)."
                     qtTrId("timetable.command.req.pu")},
                    {"pd", Spec::Number, percent,
                     //% "The chance someone gets off (0 to 99, 25 by default)."
                     qtTrId("timetable.command.req.pd")},
                    {"laststop", Spec::None, QString(),
                     //% "The player told at the last stop before whether it stops."
                     qtTrId("timetable.command.req.laststop")},
                    {"approach", Spec::None, QString(),
                     //% "The player told on the approach when it stops."
                     qtTrId("timetable.command.req.approach")},
                    {"message", Spec::None, QString(),
                     //% "Told by a message (with /approach)."
                     qtTrId("timetable.command.req.message")},
                    {"sound", Spec::None, QString(),
                     //% "Told by a sound (with /approach)."
                     qtTrId("timetable.command.req.sound")}});
    // Timing
    out << command("wait", Spec::TimingGroup, timing, Spec::Train, train,
                   //% "Waits for that train to pass first where their routes join, either way."
                   qtTrId("timetable.command.wait"), waitQualifiers);
    out << command("follow", Spec::TimingGroup, timing, Spec::Train, train,
                   //% "Follows that train: wherever their routes join again it waits until the other has passed."
                   qtTrId("timetable.command.follow"), waits);
    out << command("connect", Spec::TimingGroup, timing, Spec::Train, train,
                   //% "Waits at the station for that train to arrive (passengers changing)."
                   qtTrId("timetable.command.connect"),
                   {{"maxdelay", Spec::Number, minutes,
                     //% "Not when the other train is later than this."
                     qtTrId("timetable.command.connect.maxdelay")},
                    {"hold", Spec::Number, minutes,
                     //% "How long it stands after the other train arrives."
                     qtTrId("timetable.command.connect.hold")}});
    out << command("waitany", Spec::TimingGroup, timing, Spec::Path, path,
                   //% "Waits for any train on that path, running its way."
                   qtTrId("timetable.command.waitany"),
                   {{"both", Spec::None, QString(),
                     //% "Trains running either way."
                     qtTrId("timetable.command.waitany.both")},
                    {"opposite", Spec::None, QString(),
                     //% "Trains running the other way only."
                     qtTrId("timetable.command.waitany.opposite")}});
    out << command("callon", Spec::TimingGroup, timing, Spec::None, QString(),
                   //% "May run into a platform another train stands in."
                   qtTrId("timetable.command.callon"));
    out << command("noclaim", Spec::TimingGroup, timing, Spec::None, QString(),
                   //% "Claims no track while it waits at a signal: last at busy junctions (experimental in Open Rails)."
                   qtTrId("timetable.command.noclaim"));
    out << command("activate", Spec::TimingGroup, shunting, Spec::Train, train,
                   //% "Starts that train ($activated): arriving at the stop, at its own start (#note) or at its end (#dispose)."
                   qtTrId("timetable.command.activate"),
                   {{"depart", Spec::None, QString(),
                     //% "Leaving the stop, not arriving."
                     qtTrId("timetable.command.activate.depart")}});
    // Shunting
    out << command("attach", Spec::ShuntingGroup, shunting, Spec::Train, train,
                   //% "Attaches to that train and ends."
                   qtTrId("timetable.command.attach"),
                   {{"firstin", Spec::None, QString(),
                     //% "In first: waits for the other train to come in."
                     qtTrId("timetable.command.attach.firstin")},
                    {"setback", Spec::None, QString(),
                     //% "In first: waits for the other train to come in behind, then sets back onto it."
                     qtTrId("timetable.command.attach.setback")}});
    out << command("detach", Spec::ShuntingGroup, shunting, Spec::None, QString(),
                   //% "Detaches a part of the train."
                   qtTrId("timetable.command.detach"),
                   {{"power", Spec::None, QString(),
                     //% "The power unit (the front one first)."
                     qtTrId("timetable.command.detach.power")},
                    {"leadingpower", Spec::None, QString(),
                     //% "The front power unit."
                     qtTrId("timetable.command.detach.leadingpower")},
                    {"allleadingpower", Spec::None, QString(),
                     //% "All power units at the front."
                     qtTrId("timetable.command.detach.allleadingpower")},
                    {"trailingpower", Spec::None, QString(),
                     //% "The rear power unit."
                     qtTrId("timetable.command.detach.trailingpower")},
                    {"alltrailingpower", Spec::None, QString(),
                     //% "All power units at the rear."
                     qtTrId("timetable.command.detach.alltrailingpower")},
                    {"nonpower", Spec::None, QString(),
                     //% "All units that are not power units."
                     qtTrId("timetable.command.detach.nonpower")},
                    {"units", Spec::Number, units,
                     //% "This many units from the front."
                     qtTrId("timetable.command.detach.units")},
                    {"consist", Spec::Text, consists,
                     //% "Those consists, at either end."
                     qtTrId("timetable.command.detach.consist")},
                    {"forms", Spec::Train, train,
                     //% "The part detached forms that train."
                     qtTrId("timetable.command.detach.forms")},
                    {"static", Spec::None, QString(),
                     //% "The part detached stands as a static consist."
                     qtTrId("timetable.command.detach.static")}});
    out << command("pickup", Spec::ShuntingGroup, shunting, Spec::Train, train,
                   //% "Picks up that train, which ends."
                   qtTrId("timetable.command.pickup"),
                   {{"static", Spec::None, QString(),
                     //% "The static consist standing there instead."
                     qtTrId("timetable.command.pickup.static")}});
    out << command("transfer", Spec::ShuntingGroup, shunting, Spec::Train, train,
                   //% "Moves units between this train and that one; both go on."
                   qtTrId("timetable.command.transfer"),
                   {{"static", Spec::None, QString(),
                     //% "With the static consist standing there instead."
                     qtTrId("timetable.command.transfer.static")},
                    {"give", Spec::None, QString(),
                     //% "Gives the units named to the other train."
                     qtTrId("timetable.command.transfer.give")},
                    {"take", Spec::None, QString(),
                     //% "Takes the units named from the other train."
                     qtTrId("timetable.command.transfer.take")},
                    {"keep", Spec::None, QString(),
                     //% "Gives all but the units named."
                     qtTrId("timetable.command.transfer.keep")},
                    {"leave", Spec::None, QString(),
                     //% "Takes all but the units named."
                     qtTrId("timetable.command.transfer.leave")},
                    {"onepower", Spec::None, QString(),
                     //% "One power unit."
                     qtTrId("timetable.command.transfer.onepower")},
                    {"allpower", Spec::None, QString(),
                     //% "All power units."
                     qtTrId("timetable.command.transfer.allpower")},
                    {"nonpower", Spec::None, QString(),
                     //% "All units that are not power units."
                     qtTrId("timetable.command.transfer.nonpower")},
                    {"units", Spec::Number, units,
                     //% "This many units."
                     qtTrId("timetable.command.transfer.units")},
                    {"consist", Spec::Text, consists,
                     //% "Those consists."
                     qtTrId("timetable.command.transfer.consist")}});
    // #dispose
    out << command("forms", Spec::DisposeGroup, Spec::Dispose, Spec::Train, train,
                   //% "Forms that train of its vehicles where it ends; the new train starts at its own start time."
                   qtTrId("timetable.command.forms"), forms);
    out << command("triggers", Spec::DisposeGroup, Spec::Dispose, Spec::Train, train,
                   //% "Starts that train where it ends, with the new train's own consist."
                   qtTrId("timetable.command.triggers"), forms);
    out << command("static", Spec::DisposeGroup, Spec::Dispose, Spec::None, QString(),
                   //% "Stands as a static train where it ends."
                   qtTrId("timetable.command.dispose.static"),
                   {{"closeup", Spec::None, QString(),
                     //% "Ends close up to the end of the track."
                     qtTrId("timetable.command.dispose.static.closeup")}});
    out << command("stable", Spec::DisposeGroup, Spec::Dispose, Spec::None, QString(),
                   //% "Moves out to a stabling place, then stands there as a static train, or comes back to form or start a train."
                   qtTrId("timetable.command.stable"),
                   {{"out_path", Spec::Path, path,
                     //% "The path out to the stabling place."
                     qtTrId("timetable.command.stable.out.path")},
                    {"out_time", Spec::Time, time,
                     //% "When it moves out (none: at once)."
                     qtTrId("timetable.command.stable.out.time")},
                    {"in_path", Spec::Path, path,
                     //% "The path back to where the new train starts."
                     qtTrId("timetable.command.stable.in.path")},
                    {"in_time", Spec::Time, time,
                     //% "When it comes back (none: at once)."
                     qtTrId("timetable.command.stable.in.time")},
                    {"static", Spec::None, QString(),
                     //% "Stands as a static train once out."
                     qtTrId("timetable.command.stable.static")},
                    {"forms", Spec::Train, train,
                     //% "Forms that train once back."
                     qtTrId("timetable.command.stable.forms")},
                    {"triggers", Spec::Train, train,
                     //% "Starts that train once back, with its consist."
                     qtTrId("timetable.command.stable.triggers")},
                    {"runround", Spec::Path, path,
                     //% "Its engine runs round the train by this path."
                     qtTrId("timetable.command.stable.runround")},
                    {"rrtime", Spec::Time, time,
                     //% "When the run-round takes place."
                     qtTrId("timetable.command.stable.rrtime")},
                    {"rrpos", Spec::Text, QStringLiteral("out | stable | in"),
                     //% "Where the run-round takes place: before moving out, at the stabling place or once back."
                     qtTrId("timetable.command.stable.rrpos")},
                    {"closeup", Spec::None, QString(),
                     //% "Ends close up to the end of the track."
                     qtTrId("timetable.command.stable.closeup")},
                    {"callon", Spec::None, QString(),
                     //% "May come back into a platform another train stands in."
                     qtTrId("timetable.command.stable.callon")},
                    {"speed", Spec::Speed, QString(),
                     //% "The speed of the run-round."
                     qtTrId("timetable.command.stable.speed")},
                    {"name", Spec::Text, train,
                     //% "The name of the train while it runs round."
                     qtTrId("timetable.command.stable.name")}});
    out << command("pool", Spec::DisposeGroup, Spec::Dispose, Spec::Text, pool,
                   //% "Goes into that pool of stored trains where it ends."
                   qtTrId("timetable.command.dispose.pool"),
                   {{"direction", Spec::Text, direction,
                     //% "The way it goes onto a turntable pool (none: it reverses)."
                     qtTrId("timetable.command.dispose.pool.direction")}});
    return out;
}

}

const OrCommandSpec::QualifierSpec *OrCommandSpec::qualifier(const QString &name) const {
    for (const QualifierSpec &spec : qualifiers)
        if (spec.name == name)
            return &spec;
    return nullptr;
}

QString OrCommandSpec::GroupName(Group group) {
    switch (group) {
    case StartGroup:
        //% "Start"
        return qtTrId("timetable.command.group.start");
    case NoteGroup:
        //% "Train"
        return qtTrId("timetable.command.group.note");
    case SpeedGroup:
        //% "Speeds"
        return qtTrId("timetable.command.group.speed");
    case StationGroup:
        //% "Station stop"
        return qtTrId("timetable.command.group.station");
    case TimingGroup:
        //% "Waits and other trains"
        return qtTrId("timetable.command.group.timing");
    case ShuntingGroup:
        //% "Attaching and detaching"
        return qtTrId("timetable.command.group.shunting");
    case DisposeGroup:
        //% "At its end"
        return qtTrId("timetable.command.group.dispose");
    }
    return QString();
}

const QVector<OrCommandSpec> &OrCommandSpecs() {
    static const QVector<OrCommandSpec> specs = made();
    return specs;
}

const OrCommandSpec *OrCommandSpecAt(const QString &name, int place) {
    for (const OrCommandSpec &spec : OrCommandSpecs())
        if (spec.name == name && (spec.places & place) != 0)
            return &spec;
    return nullptr;
}
