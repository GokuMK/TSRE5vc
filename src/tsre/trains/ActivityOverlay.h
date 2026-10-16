#pragma once
#include <QString>
#include <QStringList>

class Activity;

// Open Rails' overlay of an activity: ACTIVITIES/OPENRAILS/<name>.act, the
// additions of authors who edit with MSAE (which drops them). TSRE does not
// edit overlays; it can merge one into its activity on request (task
// editor 07, decided), as Open Rails merges it: the horn settings, and for
// each event by its ID the fields and outcomes the overlay gives (one of a
// kind replaces, links add).
namespace ActivityOverlay {

// The overlay of the activity, or empty.
QString PathOf(const Activity *activity);

struct Result {
    // What the merge adds or changes; what it leaves out and why.
    QStringList added;
    QStringList skipped;
    bool readable = true;
};

// What merging the overlay at `overlayPath` would add (apply false), or
// adds it (apply true; the activity is changed).
Result Merge(Activity *activity, const QString &overlayPath, bool apply);

}
