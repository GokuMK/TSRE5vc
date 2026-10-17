#pragma once
#include <QList>

class QObject;
class QWidget;

// While a simulation exists the activity windows show the activity but do
// not edit it (task simulation 01, ActivityNotifier::editingLocked).
namespace ActivityEditLock {

// A box around a properties panel, to be locked in its place: the panel
// enables itself as it shows things, a box switched off keeps it off.
QWidget *Box(QWidget *content);
// The widgets are off while editing is locked, for as long as `owner`
// lives.
void Follow(QObject *owner, const QList<QWidget *> &widgets);

}
