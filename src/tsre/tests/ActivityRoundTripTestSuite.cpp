#include <tsre/tests/ActivityRoundTripTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/fileFunctions/SimisTextReader.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Traffic.h>

namespace {

using Kind = SimisTextReader::Kind;

// A file as blocks: a block's name, its values and its blocks.
struct Node {
    QString name;
    QStringList values;
    QVector<Node> children;
};

QString tokenText(const SimisTextReader::Token &token) {
    return QString(token.text.constData(), token.text.size());
}

// The rest of a block whose '(' is read; false when the file ends in it.
bool readBlock(SimisTextReader &reader, Node &node) {
    for (;;) {
        const Kind kind = reader.peekKind();
        if (kind == Kind::End || kind == Kind::Error)
            return false;
        if (kind == Kind::Close) {
            reader.next();
            return true;
        }
        if (kind == Kind::Open) {
            reader.next();
            Node child;
            if (!readBlock(reader, child))
                return false;
            node.children << child;
            continue;
        }
        const SimisTextReader::Token token = reader.next();
        if (token.kind == Kind::Atom && reader.peekKind() == Kind::Open) {
            reader.next();
            Node child;
            child.name = tokenText(token);
            if (!readBlock(reader, child))
                return false;
            node.children << child;
            continue;
        }
        node.values << tokenText(token);
    }
}

bool readTree(const QString &path, Node &root, QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = "cannot open";
        return false;
    }
    QString text;
    if (!SimisTextReader::decode(file.readAll(), text, error))
        return false;
    SimisTextReader reader(text);
    // The SIMIS header is a word before the first block.
    for (;;) {
        const Kind kind = reader.peekKind();
        if (kind == Kind::End)
            return true;
        if (kind == Kind::Error) {
            error = "unreadable";
            return false;
        }
        const SimisTextReader::Token token = reader.next();
        if (token.kind == Kind::Atom && reader.peekKind() == Kind::Open) {
            reader.next();
            Node child;
            child.name = tokenText(token);
            if (!readBlock(reader, child)) {
                error = "ends inside " + child.name;
                return false;
            }
            root.children << child;
        }
    }
}

// Numbers are equal within what the writers' formatting loses; words and
// strings are equal as text (quoted or not).
bool sameValue(const QString &a, const QString &b) {
    if (a == b)
        return true;
    bool okA = false, okB = false;
    const double x = a.toDouble(&okA), y = b.toDouble(&okB);
    if (!okA || !okB)
        return false;
    return std::fabs(x - y) <= 1e-6 + 1e-5 * std::max(std::fabs(x), std::fabs(y));
}

struct Differences {
    QStringList missing, added, changed;
    bool none() const { return missing.isEmpty() && added.isEmpty() && changed.isEmpty(); }
};

// The written file against the original. Blocks of one name are compared
// in their order; the order between names is free (TSRE writes its fields
// in MSTS's order and kept blocks at the end of their block).
void compare(const Node &original, const Node &written, const QString &path, Differences &out) {
    bool valuesSame = original.values.size() == written.values.size();
    for (int i = 0; valuesSame && i < original.values.size(); ++i)
        valuesSame = sameValue(original.values[i], written.values[i]);
    // Line breaks and tabs shown as escapes, to keep a difference on a line.
    const auto shown = [](const QStringList &values) {
        return values.join(' ').replace('\n', "\\n").replace('\r', "\\r").replace('\t', "\\t");
    };
    if (!valuesSame)
        out.changed << QString("%1: [%2] / [%3]").arg(path, shown(original.values), shown(written.values));
    QMap<QString, QVector<const Node*>> a, b;
    QStringList order;
    for (const Node &child : original.children) {
        const QString key = child.name.toLower();
        if (!a.contains(key) && !b.contains(key))
            order << key;
        a[key] << &child;
    }
    for (const Node &child : written.children) {
        const QString key = child.name.toLower();
        if (!a.contains(key) && !b.contains(key))
            order << key;
        b[key] << &child;
    }
    for (const QString &key : order) {
        const QVector<const Node*> &left = a[key], &right = b[key];
        const QString name = !left.isEmpty() ? left[0]->name : right[0]->name;
        for (int i = 0; i < std::min(left.size(), right.size()); ++i)
            compare(*left[i], *right[i], path + "/" + name + (left.size() > 1 ? QString("[%1]").arg(i) : QString()), out);
        for (int i = right.size(); i < left.size(); ++i)
            out.missing << path + "/" + name;
        for (int i = left.size(); i < right.size(); ++i)
            out.added << path + "/" + name;
    }
}

// Reads the file with TSRE and writes it into the folder; the written path.
QString writeWithTsre(const QString &source, const QString &folder, QString &error) {
    const QFileInfo info(source);
    const QString suffix = info.suffix().toLower();
    const QString name = info.fileName();
    const QString written = folder + "/" + name;
    if (suffix == "act") {
        Activity *activity = new Activity(source, info.absolutePath(), name);
        if (activity->loaded != 1) {
            error = "TSRE cannot read it";
            return {};
        }
        // Saving removes the activity's .asv: the copy's, not the original's.
        activity->path = folder;
        activity->pathid = written;
        activity->save();
    } else if (suffix == "srv") {
        Service *service = new Service(info.absolutePath(), name);
        if (service->loaded != 1) {
            error = "TSRE cannot read it";
            return {};
        }
        service->path = folder;
        service->save();
    } else if (suffix == "trf") {
        Traffic *traffic = new Traffic(info.absolutePath(), name);
        if (traffic->loaded != 1) {
            error = "TSRE cannot read it";
            return {};
        }
        traffic->path = folder;
        traffic->save();
    } else if (suffix == "con") {
        Consist *consist = new Consist(source, info.absolutePath(), name);
        if (consist->loaded != 1) {
            error = "TSRE cannot read it";
            return {};
        }
        consist->path = folder;
        consist->save();
    } else {
        error = "not an activity file";
        return {};
    }
    if (!QFile::exists(written)) {
        error = "not written";
        return {};
    }
    return written;
}

bool roundTrip(const QString &source, const QString &folder, Differences &differences, QString &error,
               QString *writtenPath = nullptr) {
    Node original, written;
    if (!readTree(source, original, error)) {
        error = "original: " + error;
        return false;
    }
    const QString path = writeWithTsre(source, folder, error);
    if (path.isEmpty())
        return false;
    if (!readTree(path, written, error)) {
        error = "written: " + error;
        return false;
    }
    if (writtenPath)
        *writtenPath = path;
    compare(original, written, "", differences);
    return true;
}

bool writeSimis(const QString &path, const QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QString crlf = text;
    crlf.replace("\n", "\r\n");
    file.write("\xff\xfe", 2);
    file.write(reinterpret_cast<const char*>(crlf.utf16()), crlf.size() * 2);
    return true;
}

QString readText(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QString text;
    QString error;
    SimisTextReader::decode(file.readAll(), text, error);
    return text;
}

// Open Rails additions (the manual's "Extended activities"), comments and
// unknown blocks at each level; a quoted bracket; a stop missing fields.
const char *fixtureActivity = R"(SIMISA@@@@@@@@@@JINX0a0t______

Tr_Activity (
	Serial ( 3 )
	Tr_Activity_Header (
		RouteID ( TEST )
		Name ( "Kept blocks" )
		Description ( "Line one\nLine two" )
		Briefing ( "Brief (unbalanced" )
		CompleteActivity ( 1 )
		Type ( 0 )
		Mode ( 2 )
		StartTime ( 8 0 0 )
		Season ( 1 )
		Weather ( 0 )
		PathID ( Main )
		StartingSpeed ( 0 )
		Duration ( 1 0 )
		Difficulty ( 1 )
		Animals ( 10 )
		Workers ( 5 )
		FuelWater ( 100 )
		FuelCoal ( 100 )
		FuelDiesel ( 100 )
		FuelWood ( 80 )
		FuelSand ( 90 )
		ORTSLoadStationsPopulation ( Stations )
		Comment ( "In the header (sic" )
	)
	Tr_Activity_File (
		Player_Service_Definition ( Player
			Player_Traffic_Definition ( 28800
				ArrivalTime ( 29000 )
				DepartTime ( 29060 )
				SkipCount ( 0 )
				DistanceDownPath ( 1000 )
				PlatformStartID ( 12 )
				ArrivalTime ( 29500 )
				Comment ( "Player timetable" )
			)
			UiD ( 0 )
			Efficiency ( 0.9 )
			SkipCount ( 0 )
			DistanceDownPath ( 1000 )
			PlatformStartID ( 12 )
			Efficiency ( 0.8 )
			Comment ( "Player service" )
		)
		NextServiceUID ( 3 )
		NextActivityObjectUID ( 32770 )
		ORTSAIHornAtCrossings ( 1 )
		ORTSAICrossingHornPattern ( US )
		Traffic_Definition ( Traffic
			Service_Definition ( AI 30000
				UiD ( 1 )
				Comment ( "AI instance" )
			)
			Comment ( "Traffic" )
		)
		Events (
			EventCategoryAction (
				EventTypePickUpWagons ( )
				ID ( 1 )
				Activation_Level ( 1 )
				Outcomes (
					IncActLevel ( 2 )
					DisplayMessage ( "Picked up :-(" )
					ORTSActivitySound (
						ORTSActSoundFile ( "horn.wav" Location )
						ORTSSoundLocation ( -6 14 10 5 20 )
					)
					ORTSWeatherChange (
						ORTSOvercast ( 0.8 30 )
						ORTSFog ( 500 60 )
						ORTSPrecipitationIntensity ( 0.01 30 )
						ORTSPrecipitationLiquidity ( 1 30 )
					)
					ORTSRestartWaitingTrain (
						ORTSWaitingTrainToRestart ( "AI" 30000 )
						ORTSDelayToRestart ( 60 )
						ORTSMatchingWPDelay ( 31200 )
					)
				)
				Name ( "Pick up" )
				Wagon_List (
					UiD ( 2147581952 )
					SidingItem ( 4294967295 )
					Description ( "" )
					Comment ( wagons )
				)
				Reversible_Event ( )
				Comment ( "Event" )
			)
			EventCategoryLocation (
				EventTypeLocation ( )
				ID ( 2 )
				Activation_Level ( 1 )
				Outcomes (
					ActivitySuccess ( )
				)
				Name ( "Here" )
				Location ( -6 14 10.5 -20.25 50 )
				TriggerOnStop ( 1 )
				ORTSContinue ( 5 )
				ORTSTriggeringTrain ( "AI" 30000 )
				ORTSActSoundFile ( "old.wav" Cab )
			)
			EventCategoryTime (
				EventTypeTime ( )
				ID ( 3 )
				Activation_Level ( 1 )
				Outcomes ( )
				Name ( "Time" )
				Time ( 60 )
			)
			Comment ( "Events" )
		)
		ActivityObjects (
			ActivityObject (
				ObjectType ( WagonsList )
				Train_Config (
					TrainCfg ( "loose"
						Name ( "Loose" )
						Serial ( 1 )
						MaxVelocity ( 0.00000 0.00100 )
						NextWagonUID ( 1 )
						Durability ( 1.00000 )
						Wagon (
							WagonData ( missing_wagon missing_folder )
							UiD ( 0 )
							LoadData ( coal missing_folder Full )
						)
						ORTSEOT (
							EOTData ( eot missing_folder )
						)
					)
					Comment ( "Train_Config" )
				)
				Direction ( 1 )
				ID ( 32769 )
				Tile ( -6 14 10 10 )
				Comment ( "Object" )
			)
			Comment ( "Objects" )
		)
		ActivityRestrictedSpeedZones (
			ActivityRestrictedSpeedZone (
				StartPosition ( -6 14 10 10 )
				EndPosition ( -6 14 20 20 )
				Comment ( "Zone" )
			)
			Comment ( "Zones" )
		)
		ActivityFailedSignals (
			ActivityFailedSignal ( 3 )
			Comment ( "Signals" )
		)
		PlatformNumPassengersWaiting (
			PlatformData ( 41 20 )
			PlatformData ( 48 35 )
			Comment ( "Passengers" )
		)
		Comment ( "Activity file" )
	)
	Comment ( "Activity" )
)
)";

const char *fixtureService = R"(SIMISA@@@@@@@@@@JINX0v0t______

Service_Definition (
	Serial ( 1 )
	Name ( "Player" )
	Train_Config ( Train )
	PathID ( "Main" )
	MaxWheelAcceleration ( 0 )
	Efficiency ( 0.9 )
	TimeTable (
		StartingSpeed ( 0 )
		EndingSpeed ( 0 )
		StartInWorld ( 0 )
		EndInWorld ( 0 )
		StationStop (
			PlatformStartID ( 12 )
			DistanceDownPath ( 1000 )
			SkipCount ( 0 )
			Comment ( "Stop" )
		)
		Comment ( "Timetable" )
	)
	Comment ( "Service" )
)
)";

const char *fixtureTraffic = R"(SIMISA@@@@@@@@@@JINX0f0t______

Traffic_Definition ( Traffic
	Serial ( 1 )
	Service_Definition ( AI 30000
		ArrivalTime ( 30100 )
		DepartTime ( 30160 )
		SkipCount ( 0 )
		DistanceDownPath ( 500 )
		PlatformStartID ( 12 )
		Comment ( "Instance" )
	)
	Comment ( "Traffic" )
)
)";

const char *fixtureConsist = R"(SIMISA@@@@@@@@@@JINX0D0t______

Train (
	TrainCfg ( "Train"
		Name ( "Train" )
		Serial ( 1 )
		MaxVelocity ( 20.00000 0.50000 )
		NextWagonUID ( 2 )
		Durability ( 1.00000 )
		Engine (
			UiD ( 0 )
			EngineData ( missing_engine missing_folder )
			Comment ( "Engine" )
		)
		Wagon (
			WagonData ( missing_wagon missing_folder )
			UiD ( 1 )
			LoadData ( coal missing_folder Full )
		)
		ORTSEOT (
			EOTData ( eot missing_folder )
		)
		ORTSTrainControlSystemParameters ( "etcs.ini" )
	)
	Comment ( "Train" )
)
)";

}

int TsreTests::runActivityRoundTripSuite(const QString &contentRoot, bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:activity-roundtrip] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:activity-roundtrip] FAIL" << name; }
    };
    EngLib engines;
    const QScopedValueRollback<EngLib*> engineLibrary(Game::currentEngLib,
            Game::currentEngLib != nullptr ? Game::currentEngLib : &engines);

    // The written fixtures: nothing lost, nothing added, a second save the
    // same as the first.
    QTemporaryDir temp;
    const QString source = temp.path() + "/source", first = temp.path() + "/first", second = temp.path() + "/second";
    for (const QString &dir : {source, first, second})
        QDir().mkpath(dir);
    const QList<QPair<QString, const char*>> fixtures = {
        {"Kept.act", fixtureActivity}, {"Player.srv", fixtureService},
        {"Traffic.trf", fixtureTraffic}, {"Train.con", fixtureConsist}};
    for (const auto &fixture : fixtures) {
        const QString file = source + "/" + fixture.first;
        check(writeSimis(file, fixture.second), "write " + fixture.first);
        Differences differences;
        QString error, written;
        const bool ok = roundTrip(file, first, differences, error, &written);
        for (const QString &line : differences.missing + differences.changed + differences.added)
            qWarning().noquote() << "[tests:activity-roundtrip]" << fixture.first << line;
        check(ok && differences.none(), fixture.first + " keeps every block" + (error.isEmpty() ? "" : ": " + error));
        QString again;
        Differences secondDifferences;
        const bool twice = ok && roundTrip(written, second, secondDifferences, error, &again);
        check(twice && readText(written) == readText(again), fixture.first + " written again unchanged");
    }

    // The activity read from the fixture: values in their places.
    {
        Activity activity(source + "/Kept.act", source, QString("Kept.act"));
        check(activity.loaded == 1 && activity.platformNumPassengersWaiting.size() == 2
              && activity.platformNumPassengersWaiting[0] == qMakePair(41, 20),
              "PlatformData: the platform, then the passengers");
        check(activity.keptFile.size() == 2 && activity.keptFile[0].startsWith("ORTSAICrossingHornPattern"),
              "Open Rails' horn pattern kept in Tr_Activity_File");
        check(activity.header && activity.header->kept.size() == 4 && activity.header->kept[3] == "Comment ( \"In the header (sic\" )",
              "a comment with a quoted bracket kept as written");
        const QString text = readText(first + "/Kept.act");
        check(text.indexOf("ORTSAICrossingHornPattern") > text.indexOf("NextActivityObjectUID"),
              "Open Rails' activity additions written after NextActivityObjectUID");
        check(text.contains("old.wav") && text.count("ORTSActSoundFile") == 2,
              "the old event sound written once, the outcome's kept");
    }

    // The content under the root: every activity, service and traffic file
    // (consists are the Consist Editor's: it computes their MaxVelocity),
    // the differences by kind.
    if (!contentRoot.isEmpty()) {
        QDirIterator it(contentRoot, {"*.act", "*.srv", "*.trf", "*.ACT", "*.SRV", "*.TRF"},
                        QDir::Files, QDirIterator::Subdirectories);
        int files = 0, unreadable = 0, different = 0;
        QMap<QString, int> kinds;
        QMap<QString, QString> examples;
        QDir().mkpath(temp.path() + "/content");
        int folder = 0;
        while (it.hasNext()) {
            const QString file = it.next();
            ++files;
            // One folder per file: files of different routes share names.
            const QString out = temp.path() + QString("/content/%1").arg(folder++);
            QDir().mkpath(out);
            Differences differences;
            QString error;
            if (!roundTrip(file, out, differences, error)) {
                ++unreadable;
                if (verbose)
                    qInfo().noquote() << "[tests:activity-roundtrip] unreadable" << file << error;
                continue;
            }
            if (!differences.none())
                ++different;
            const auto count = [&](const QStringList &lines, const QString &kind) {
                for (QString line : lines) {
                    // By the blocks' names, without indexes and values.
                    line = line.section(':', 0, 0).replace(QRegularExpression("\\[\\d+\\]"), "");
                    const QString key = kind + " " + line;
                    if (!kinds.contains(key))
                        examples[key] = file;
                    ++kinds[key];
                }
            };
            count(differences.missing, "missing");
            count(differences.changed, "changed");
            count(differences.added, "added");
            if (verbose)
                for (const QString &line : differences.missing + differences.changed + differences.added)
                    qInfo().noquote() << "[tests:activity-roundtrip]" << file << line;
        }
        for (auto kind = kinds.begin(); kind != kinds.end(); ++kind)
            qInfo().noquote() << "[tests:activity-roundtrip]" << kind.value() << kind.key() << "e.g." << examples[kind.key()];
        qInfo().noquote() << QString("[tests:activity-roundtrip] content: %1 files, %2 not read, %3 with differences")
                             .arg(files).arg(unreadable).arg(different);
        bool lost = false;
        for (auto kind = kinds.begin(); kind != kinds.end(); ++kind)
            lost = lost || kind.key().startsWith("missing") || kind.key().startsWith("changed");
        check(!lost, "content: nothing lost or changed");
    }

    qInfo().noquote() << QString("activity-roundtrip: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
