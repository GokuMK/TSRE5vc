#include <tsre/trains/ActivityNotifier.h>

ActivityNotifier *ActivityNotifier::instance() {
    static ActivityNotifier notifier;
    return &notifier;
}
