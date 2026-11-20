// Task explore 02: the Explore window's lists, read without a loaded route.

#include "ExploreListsTestSuite.h"
#include "TokenTestSupport.h"

#include <explore/ExploreLists.h>
#include <explore/ExplorePanel.h>
#include <tsre/Game.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/script/ScriptState.h>
#include <tsre/ui/UiRuntime.h>

#include <RmlUi/Core.h>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>

namespace {

// Draws nothing: the documents are only laid out.
class NoRender : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &) override {
        dimensions = Rml::Vector2i(4, 4);
        return 1;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

template <typename T>
int countOf(const QVector<T> &items, const std::function<bool(const T &)> &match) {
    return int(std::count_if(items.begin(), items.end(), match));
}

}

int TsreTests::runExploreListsSuite(bool verbose) {
    TokenTest::Suite test{"[tests:explore-lists]", verbose};
    const QString root = QDir::cleanPath(Game::root);
    const bool europe = QFileInfo(root + "/ROUTES/EUROPE1").isDir();
    const bool prudnik = QFileInfo(root + "/ROUTES/PRUDNIK/ACTIVITIES/OPENRAILS/PSK_2026.timetable_or").isFile();
    if (!europe && !prudnik) {
        qInfo().noquote() << "[tests:explore-lists] no EUROPE1 or PRUDNIK in the game root" << root
                          << ": nothing checked (--game-root)";
        return test.finish();
    }

    // The days' seasons, the south's half a year on.
    test.check(Explore::Lists::seasonOf(QDate(2026, 1, 15), 50.0) == 3
                       && Explore::Lists::seasonOf(QDate(2026, 1, 15), -30.0) == 1
                       && Explore::Lists::seasonOf(QDate(2026, 7, 1), 50.0) == 1
                       && Explore::Lists::seasonOf(QDate(2026, 3, 1), 50.0) == 0
                       && Explore::Lists::seasonOf(QDate(2026, 11, 30), 50.0) == 2,
               "a day's season at a latitude (meteorological; the south's half a year on)");

    QElapsedTimer timer;
    timer.start();
    const QVector<Explore::Lists::RouteInfo> routes = Explore::Lists::routes(root);
    qInfo().noquote() << QString("[tests:explore-lists] %1 routes read in %2 ms").arg(routes.size()).arg(timer.elapsed());
    const auto routeOf = [&routes](const QString &folder) -> const Explore::Lists::RouteInfo * {
        for (const Explore::Lists::RouteInfo &r : routes)
            if (r.folder == folder)
                return &r;
        return nullptr;
    };

    if (europe) {
        const Explore::Lists::RouteInfo *route = routeOf("EUROPE1");
        test.check(route != nullptr && !route->name.isEmpty() && !route->id.isEmpty(),
                   "EUROPE1 listed with its name and RouteID");
        if (route != nullptr) {
            test.check(route->latitudeKnown && route->latitude > 50.0 && route->latitude < 60.0,
                       QString("EUROPE1's latitude from its TRK (%1)").arg(route->latitude));
            test.check(!route->loadingScreen.isEmpty() && QFileInfo(route->loadingScreen).isFile(),
                       "its loading screen found: " + route->loadingScreen);
            timer.restart();
            const QVector<Explore::Lists::ActivityInfo> activities = Explore::Lists::activities(root, *route);
            qInfo().noquote() << QString("[tests:explore-lists] EUROPE1: %1 activities in %2 ms")
                                         .arg(activities.size()).arg(timer.elapsed());
            const int files = QDir(root + "/ROUTES/EUROPE1/ACTIVITIES").entryList({"*.act"}, QDir::Files).size();
            test.check(activities.size() == files, QString("every activity listed (%1 of %2)").arg(activities.size()).arg(files));
            const Explore::Lists::ActivityInfo *local = nullptr;
            for (const Explore::Lists::ActivityInfo &a : activities)
                if (a.file == "lclsrvce.act")
                    local = &a;
            test.check(local != nullptr, "lclsrvce listed");
            if (local != nullptr)
                test.check(!local->name.isEmpty() && local->startTime > 0 && local->season >= 0 && local->season <= 3
                                   && local->weather >= 0 && local->weather <= 2 && local->difficulty >= 0
                                   && local->duration > 0 && !local->consist.isEmpty() && !local->path.isEmpty()
                                   && !local->description.isEmpty(),
                           QString("lclsrvce's values: %1, %2 s, season %3, weather %4, %5 min, %6 on %7")
                                   .arg(local->name).arg(local->startTime).arg(local->season).arg(local->weather)
                                   .arg(local->duration).arg(local->consist, local->path));
            QStringList names;
            for (const Explore::Lists::ActivityInfo &a : activities)
                names << a.name;
            QStringList sorted = names;
            std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
                return QString::compare(a, b, Qt::CaseInsensitive) < 0;
            });
            test.check(names == sorted, "activities sorted by name");

            const QVector<Explore::Lists::PathInfo> paths = Explore::Lists::playerPaths(root, "EUROPE1");
            const auto has = [&paths](const QString &file) {
                return countOf<Explore::Lists::PathInfo>(paths, [&file](const Explore::Lists::PathInfo &p) {
                           return p.file == file;
                       }) == 1;
            };
            test.check(has("Local Service.pat") && !has("Local Service (Traffic).pat")
                               && !has("home stretch (traffic).pat") && has("Settle - Carlisle.pat"),
                       QString("player paths only (%1; 0x20 left out)").arg(paths.size()));
            test.check(countOf<Explore::Lists::PathInfo>(paths, [](const Explore::Lists::PathInfo &p) {
                           return p.start.isEmpty() || p.end.isEmpty();
                       }) == 0, "each path with its start and end");
        }
        timer.restart();
        const QVector<Explore::Lists::ConsistInfo> consists = Explore::Lists::drivableConsists(root);
        qInfo().noquote() << QString("[tests:explore-lists] %1 consists with an engine of %2, in %3 ms")
                                     .arg(consists.size())
                                     .arg(QDir(root + "/TRAINS/CONSISTS").entryList({"*.con"}, QDir::Files).size())
                                     .arg(timer.elapsed());
        test.check(!consists.isEmpty(), "consists with an engine");
        test.check(countOf<Explore::Lists::ConsistInfo>(consists, [&root](const Explore::Lists::ConsistInfo &c) {
                       return c.engineName.isEmpty() || !ContentPath::readable(root + "/TRAINS/TRAINSET/" + c.engine);
                   }) == 0, "each one's engine there, named");
    }

    if (prudnik) {
        const QVector<Explore::Lists::TimetableSetInfo> sets = Explore::Lists::timetableSets(root, "PRUDNIK");
        test.check(countOf<Explore::Lists::TimetableSetInfo>(sets, [](const Explore::Lists::TimetableSetInfo &s) {
                       return s.file == "PSK_2026.timetable_or" && !s.name.isEmpty();
                   }) == 1, "PRUDNIK's PSK_2026 listed, named");
        timer.restart();
        const QVector<Explore::Lists::TrainInfo> trains =
                Explore::Lists::timetableTrains(root, "PRUDNIK", "PSK_2026.timetable_or");
        const int drivable = countOf<Explore::Lists::TrainInfo>(trains, [](const Explore::Lists::TrainInfo &t) {
            return t.drivable;
        });
        qInfo().noquote() << QString("[tests:explore-lists] PSK_2026: %1 trains, %2 drivable, in %3 ms")
                                     .arg(trains.size()).arg(drivable).arg(timer.elapsed());
        test.check(trains.size() > 100 && drivable > 0, "PSK_2026's trains, some drivable");
        test.check(countOf<Explore::Lists::TrainInfo>(trains, [](const Explore::Lists::TrainInfo &t) {
                       return t.drivable && t.engine.isEmpty();
                   }) == 0, "a drivable train's engine named");
        QStringList unplaced;
        for (const Explore::Lists::TrainInfo &t : trains)
            if (t.name.isEmpty() || t.from.isEmpty() || t.to.isEmpty())
                unplaced << t.name + " (" + t.from + " - " + t.to + ")";
        test.check(unplaced.isEmpty(), "each train with its name, first and last station"
                                           + (unplaced.isEmpty() ? QString() : ": not " + unplaced.mid(0, 6).join(", ")));

        // The table through the window's model.
        const QString folder = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/ui");
        const bool started = Ui::Runtime::get() != nullptr || Ui::Runtime::start(folder);
        test.check(started, "RmlUi started");
        NoRender render;
        Rml::Context *context = started ? Rml::CreateContext("explore-lists", Rml::Vector2i(1920, 1080), &render) : nullptr;
        if (context != nullptr) {
            QTemporaryDir temporary;
            Explore::Panel::setRememberedFile(temporary.filePath("explore.json"));
            {
                bool started = false;
                Explore::Panel panel(*context, root, "PRUDNIK", [&started](const Explore::RunChoice &) { started = true; });
                Rml::ElementDocument *document = nullptr;
                {
                    Script::State::Deadline deadline(Ui::Runtime::get()->lua());
                    document = context->LoadDocument((folder + "/explore-menu.rml").toStdString());
                }
                test.check(document != nullptr, "the menu's document binds the model");
                panel.setDocument(document);
                panel.open();
                test.check(panel.routeNames().contains("PRUDNIK") && panel.chooseRun("PSK_2026.timetable_or"),
                           "PRUDNIK chosen first (the Load window's), PSK_2026 chosen");
                const QStringList shown = panel.trainNames();
                test.check(shown.size() == drivable, QString("drivable only at first (%1)").arg(shown.size()));
                panel.setDrivableOnly(false);
                test.check(panel.trainNames().size() == trains.size(), "all trains shown");
                panel.setDrivableOnly(true);
                panel.sortTrains("start");
                QVector<int> starts;
                for (const QString &name : panel.trainNames())
                    for (const Explore::Lists::TrainInfo &t : trains)
                        if (t.name == name)
                            starts << t.startTime;
                test.check(!starts.isEmpty() && std::is_sorted(starts.begin(), starts.end()), "sorted by start");
                panel.sortTrains("start");
                QVector<int> back;
                for (const QString &name : panel.trainNames())
                    for (const Explore::Lists::TrainInfo &t : trains)
                        if (t.name == name)
                            back << t.startTime;
                test.check(!back.isEmpty() && std::is_sorted(back.rbegin(), back.rend()), "again: the other way");
                const QString some = shown.isEmpty() ? QString() : shown.at(shown.size() / 2);
                panel.setFilter(some.left(4));
                const QStringList filtered = panel.trainNames();
                test.check(!filtered.isEmpty() && filtered.contains(some) && filtered.size() < shown.size(),
                           QString("the filter '%1': %2 trains").arg(some.left(4)).arg(filtered.size()));
                test.check(!panel.ready() && panel.chooseTrain(some) && panel.ready(),
                           "a train chosen: Explore can start");
                context->Update();
                Rml::Element *rows = document != nullptr ? document->GetElementById("trains") : nullptr;
                test.check(rows != nullptr && rows->GetNumChildren() >= filtered.size(),
                           QString("the table's rows made (%1)").arg(rows != nullptr ? rows->GetNumChildren() : -1));
                test.check(panel.go() && started, "Explore: the run given to start");
                const Explore::RunChoice choice = panel.choice();
                test.check(choice.kind == Explore::RunChoice::Kind::Timetable && choice.route == "PRUDNIK"
                                   && choice.file == "PSK_2026.timetable_or" && choice.train == some
                                   && choice.day.isValid(),
                           "the run: PSK_2026's train on PRUDNIK, a day");
                test.check(QFileInfo(temporary.filePath("explore.json")).isFile(), "the choices remembered");
                if (document != nullptr)
                    document->Close();
                context->Update();
            }
            Explore::Panel::setRememberedFile(QString());
            Rml::RemoveContext("explore-lists");
        }
    }
    return test.finish();
}
