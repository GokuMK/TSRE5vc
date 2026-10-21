#include <tsre/sim/WorkPlanning.h>

#include <QApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QProgressDialog>
#include <QThread>
#include <atomic>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/WorkPlanner.h>

namespace Sim {

namespace {

// How long the planning may take before the dialog shows, ms.
constexpr int QuietPlanning = 400;

}

bool PlanWorkOrders(SimulationInput &input, QString *error) {
    bool wagons = false;
    for (const EventPlan &event : input.events)
        wagons = wagons || !event.cars.empty();
    if (!wagons)
        return true;
    std::atomic_bool cancel{false};
    //% "Planning the player's work orders"
    QProgressDialog progress(qtTrId("simulation.work.planning"),
                             //% "Cancel"
                             qtTrId("simulation.work.cancel"), 0, 0, QApplication::activeWindow());
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    QEventLoop loop;
    QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&] {
        cancel = true;
        //% "Cancelling the planning"
        progress.setLabelText(qtTrId("simulation.work.cancelling"));
        progress.setCancelButton(nullptr);
    });
    WorkProgress report;
    report.cancel = &cancel;
    report.report = [&progress, &cancel](int done, int total) {
        QMetaObject::invokeMethod(&progress, [&progress, &cancel, done, total] {
            if (cancel)
                return;
            progress.setRange(0, total);
            progress.setValue(done);
            //% "Planning the player's work orders: %1 of %2 done"
            progress.setLabelText(qtTrId("simulation.work.progress").arg(done).arg(total));
        }, Qt::QueuedConnection);
    };
    bool applied = true;
    QThread *worker = QThread::create([&] { applied = ApplyWork(input, &report); });
    QObject::connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    worker->start();
    // Shown only when it takes a while.
    if (!worker->wait(QDeadlineTimer(QuietPlanning))) {
        progress.show();
        loop.exec();
    }
    worker->wait();
    delete worker;
    progress.hide();
    if (!applied && error != nullptr)
        //% "The planning of the player's work orders was cancelled."
        *error = qtTrId("simulation.work.cancelled");
    return applied;
}

}
