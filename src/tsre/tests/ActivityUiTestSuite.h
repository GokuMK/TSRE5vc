#pragma once
#include <QString>
namespace TsreTests {
// The activity windows on a small route written for the test: every field,
// list and button used with nothing shown, with objects shown and after
// deletes. A crash fails the run; looking at objects must not change them.
int runActivityUiSuite(bool verbose);
}
