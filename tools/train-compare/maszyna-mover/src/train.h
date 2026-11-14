// This file is part of TSRE5's train comparison tool (task driving 10): it
// stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
// 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
// is: this Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

// A train of MaSzyna vehicles on a straight endless track, stepped the way the game steps it.
//
// What the game does around the physics (vehicle/DynObj.cpp TDynamicObject::Init, AttachNext, Update,
// FastUpdate, update_neighbours, GetEPP; DynObj.cpp vehicle_table::update; application/drivermode.cpp
// driver_mode::update; the parts of vehicle/Driver.cpp and vehicle/Train.cpp a driven train goes
// through) is replicated here, in the same order, on MaSzyna's own mover objects (McZapkie/Mover.cpp).
// Each replicated step names the original it follows; what isn't replicated is listed in main.cpp.

#pragma once

#include "stdafx.h"
#include "vehicle/DynObj.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct track_conditions {
	double friction{0.15}; // TTrack::fFriction default
	double grade{0.0}; // rise/run, + up in the direction the train's head faces
	double line_voltage{3000.0}; // V on the wire, at the pantographs
};

class vehicle : public TDynamicObject {
public:
	std::unique_ptr<TMoverParameters> mover;
	std::string name;
	int iDirection{1}; // TDynamicObject::iDirection: 1 its front (coupler 0) faces the train's head, 0 flipped
	double position{0.0}; // vPosition along the track [m], world frame (+ towards the train's head)
	bool enabled{true}; // bEnabled
	bool mech_inside{false}; // MechInside: the player's cab is in this vehicle (TTrain::Init)
	bool has_driver{false}; // Mechanik != nullptr (the primary driver)
	int pantographs{0}; // iAnimType[ANIM_PANTS]: pantographs of the 3D model, see train.cpp
	std::array<bool, 2> pantograph_on_wire{false, false}; // stands for the pantograph arm reaching the wire
	double no_volt_time{0.0}; // NoVoltTime
	TTrackShape ts; // TDynamicObject::ts
	TTrackParam tp; // TDynamicObject::tp
	TTractionParam traction; // TDynamicObject::tmpTraction
	// the loco's traction at the rails [N, vehicle frame] after the wheel slip limit: ComputeTotalForce's FTrain
	// taken right after the call, less the coupler forces and the gravity it adds
	double traction_force{0.0};

	int DirectionGet() const { return iDirection + iDirection - 1; }
	vehicle *PrevConnected() const { return static_cast<vehicle *>(mover->Neighbours[end::front].vehicle); }
	vehicle *NextConnected() const { return static_cast<vehicle *>(mover->Neighbours[end::rear].vehicle); }
	int PrevConnectedNo() const { return mover->Neighbours[end::front].vehicle_end; }
	int NextConnectedNo() const { return mover->Neighbours[end::rear].vehicle_end; }
	// TDynamicObject::Next()/Prev(): the neighbour along the train, if coupled with any of the given flags
	vehicle *Next(int C = -1) const {
		return (mover->Couplers[iDirection].CouplingFlag & C) ? static_cast<vehicle *>(mover->Neighbours[iDirection].vehicle) : nullptr;
	}
	vehicle *Prev(int C = -1) const {
		return (mover->Couplers[iDirection ^ 1].CouplingFlag & C) ? static_cast<vehicle *>(mover->Neighbours[iDirection ^ 1].vehicle) : nullptr;
	}
};

struct harness_options {
	std::string dynamic_dir; // MaSzyna's dynamic/ folder
	double frame_dt{0.01}; // the game's frame time
	bool keep_awake{true}; // keep MaSzyna from switching off the physics of standing vehicles without a cab
	bool trainman{true}; // Global.AITrainman: the virtual trainman releasing overcharged brakes at a stand
	unsigned seed{1}; // the session's random seed
	double preroll_max{1800.0}; // s, hot start preparation limit
	bool verbose{false};
};

class train {
public:
	using event_sink = std::function<void(std::string const &)>;

	train(harness_options Options, track_conditions Track, event_sink Events);

	// TDynamicObject::Init for each vehicle, front to back; Cab 1 (or -1 if flipped) in the first
	void add_vehicle(std::string const &Folder, std::string const &Fiz, bool Flipped);
	// state_serializer::deserialize_endtrainset: AttachNext for each pair, then the driver's CheckVehicles
	void couple();
	// the loco brought to the state MaSzyna's AI leaves it in (TController::PrepareEngine), the train at a
	// stand until the pipes and reservoirs have settled; returns the preparation time used
	double hot_start();
	// all vehicles moving at Speed km/h towards the train's head
	void set_speed(double Kmh);

	// one frame of the game: driver_mode::update -> state_manager::update -> vehicle_table::update
	void frame();

	// the driver's controls, as the cab (TTrain::OnCommand_*) applies them
	bool set_direction(int Direction);
	bool main_controller_step(int Towards); // +1 IncMainCtrl(1), -1 DecMainCtrl(1)
	bool second_controller_step(int Towards);
	void set_train_brake(double Position);
	void set_local_brake(double Position); // 0..LocalBrakePosNo
	void acknowledge_vigilance(); // alerter button press and release

	std::vector<std::unique_ptr<vehicle>> const &vehicles() const { return m_vehicles; }
	vehicle &loco() { return *m_vehicles.front(); }
	double time() const { return m_time; }
	// the driver's valve's control reservoir (FV4a CP); NaN for other handles
	double driver_valve_chamber() const;
	// head's travel since set_origin(), + towards the train's head
	double distance() const { return loco_world_position() - m_origin; }
	void set_origin() { m_origin = loco_world_position(); m_time = 0.0; }
	int trainman_releases() const { return m_trainman_releases; }

private:
	double loco_world_position() const { return m_vehicles.front()->position; }
	void update_neighbours(vehicle &Vehicle);
	void update_force(vehicle &Vehicle, double Dt);
	void fast_update(vehicle &Vehicle, double Dt);
	void update(vehicle &Vehicle, double Dt, double Dt1);
	void move(vehicle &Vehicle, double Distance);
	double get_epp(vehicle &Vehicle) const;
	void track_shape(vehicle &Vehicle) const;
	TLocation location(vehicle const &Vehicle) const;
	void driver_update(vehicle &Vehicle, double Dt);
	void trainman_check();
	void prepare_engine();
	void security_reset_as_ai();
	void watch_events();
	void event(std::string const &Text) const;

	harness_options m_options;
	track_conditions m_track;
	event_sink m_events;
	std::vector<std::unique_ptr<vehicle>> m_vehicles;
	double m_time{0.0};
	double m_origin{0.0};
	bool m_preparing{false};
	double m_reaction_timer{0.0};
	double m_reaction_time{0.5};
	int m_trainman_releases{0};
	bool m_watched_ready{false}; // the first watch pass only records
	// watched state, for the event log
	struct watched {
		bool mains{false}, fuse{false}, stlin{false}, slipping{false}, convovld{false}, compressor{false}, alert{false}, braking{false};
		int damage{0};
		std::array<int, 2> coupling{0, 0};
		bool releaser{false};
	};
	std::vector<watched> m_watched;
};
