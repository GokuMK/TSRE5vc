#include <routeEditor/activity/ActivityEditLock.h>

#include <QVBoxLayout>
#include <QWidget>
#include <tsre/trains/ActivityNotifier.h>

namespace ActivityEditLock {

QWidget *Box(QWidget *content) {
    QWidget *box = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(content);
    return box;
}

void Follow(QObject *owner, const QList<QWidget *> &widgets) {
    auto apply = [widgets](bool locked) {
        for (QWidget *widget : widgets)
            widget->setEnabled(!locked);
    };
    QObject::connect(ActivityNotifier::instance(), &ActivityNotifier::editingLockChanged, owner, apply);
    apply(ActivityNotifier::editingLocked());
}

}
