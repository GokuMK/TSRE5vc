#pragma once
#include <QStringList>
#include <QVector>
#include <tsre/trains/Path.h>
#include <tsre/trains/Service.h>

// Keeping what uses a service's stops in step with them (task editor 07,
// phase 5): the activities and traffic that run the service keep copies of
// its stops with their own times.
namespace ServiceStops {

// Each stop's pass in `passes`: the pass at its SkipCount when that pass
// is at its platform, else the pass meeting its platform item nearest its
// distance; -1 when no pass is at its platform.
QVector<int> Passes(const QVector<Service::StationStop> &stops, const QVector<Path::PathObject> &passes);

// The platform a track item belongs to: the lower id of its two items.
int Platform(int trItemId);

// The service's stops changed from `before`: new stop j was stop from[j]
// before, or is new (-1). The activities and traffic running the service
// take the new stops; a kept stop keeps its times and efficiency, a new one
// takes the departure of the stop before it. Changes are kept for undo
// first. The names of the activities and traffic changed.
QStringList Follow(Service *service, const QVector<Service::StationStop> &before, const QVector<int> &from);

}
