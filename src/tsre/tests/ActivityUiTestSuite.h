#pragma once
#include <QString>
namespace TsreTests {
// The activity windows on a small route written for the test: every field,
// list and button used with nothing shown, with objects shown and after
// deletes. A crash fails the run; looking at objects must not change them.
int runActivityUiSuite(bool verbose);
// The activity windows with an Open Rails timetable (task simulation 08):
// the kind chosen, the set, its trains' times edited, undone and saved.
int runTimetableUiSuite(bool verbose);
}
