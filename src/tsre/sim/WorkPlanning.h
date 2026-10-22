#pragma once
#include <QString>

namespace Sim {

struct SimulationInput;

// The player's work orders planned and given to its train (ApplyWork, task
// 07) on a worker thread while the main thread waits: a dialog with the
// progress and Cancel when it takes longer than a moment. False when the
// user cancelled it (`error` says so), the input then as it was.
bool PlanWorkOrders(SimulationInput &input, QString *error);

}
