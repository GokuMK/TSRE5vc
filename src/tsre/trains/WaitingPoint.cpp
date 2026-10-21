#include <tsre/trains/WaitingPoint.h>

#include <QCoreApplication>
#include <algorithm>

WaitingPoint::Point WaitingPoint::decode(int value) {
    Point point;
    if (value >= 30000 && value <= 32359 && (value - 30000) % 100 < 60) {
        point.kind = UntilTime;
        point.hour = (value - 30000) / 100;
        point.minute = (value - 30000) % 100;
    } else if (value >= 40000 && value <= 59999) {
        point.kind = value < 50000 ? KeepFirst : KeepLast;
        point.cars = (value % 10000) / 100;
        point.seconds = value % 100;
    } else if (value >= 60011 && value <= 60020) {
        point.kind = Horn;
        point.seconds = value - 60010;
    } else if (value == 60021) {
        point.kind = CrossingHorn;
    } else if (value == 60001) {
        point.kind = Attach;
    } else if (value == 60002) {
        point.kind = Permission;
        point.seconds = 20;
    } else {
        point.seconds = value;
    }
    return point;
}

int WaitingPoint::encode(const Point &point) {
    switch (point.kind) {
    case UntilTime:
        return 30000 + std::clamp(point.hour, 0, 23) * 100 + std::clamp(point.minute, 0, 59);
    case KeepFirst:
    case KeepLast:
        return (point.kind == KeepFirst ? 40000 : 50000) + std::clamp(point.cars, 0, 99) * 100
               + std::clamp(point.seconds, 0, 99);
    case Horn:
        return 60010 + std::clamp(point.seconds, 1, 10);
    case CrossingHorn:
        return 60021;
    case Attach:
        return 60001;
    case Permission:
        return 60002;
    default:
        return std::clamp(point.seconds, 0, 29999);
    }
}

QString WaitingPoint::describe(int value) {
    const Point point = decode(value);
    switch (point.kind) {
    case UntilTime:
        //% "until %1"
        return qtTrId("waiting.point.until").arg(QString("%1:%2").arg(point.hour, 2, 10, QChar('0'))
                                                  .arg(point.minute, 2, 10, QChar('0')));
    case KeepFirst:
        //% "uncouple, keep the first %1 cars, wait %2 s"
        return qtTrId("waiting.point.keep.first").arg(point.cars).arg(point.seconds);
    case KeepLast:
        //% "uncouple, keep the last %1 cars, wait %2 s"
        return qtTrId("waiting.point.keep.last").arg(point.cars).arg(point.seconds);
    case Horn:
        //% "horn for %1 s, no stop"
        return qtTrId("waiting.point.horn").arg(point.seconds);
    case CrossingHorn:
        //% "level crossing horn, no stop"
        return qtTrId("waiting.point.crossing.horn");
    case Attach:
        //% "attach to the train standing ahead and join it"
        return qtTrId("waiting.point.attach");
    case Permission:
        //% "ask to pass the next signal, wait 20 s"
        return qtTrId("waiting.point.permission");
    default:
        //% "%1 s"
        return qtTrId("waiting.point.seconds").arg(point.seconds);
    }
}
