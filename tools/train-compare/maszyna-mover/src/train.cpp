// This file is part of TSRE5's train comparison tool (task driving 10): it
// stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
// 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
// is: this Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

#include "train.h"

#include "Oerlikon_ESt.h"
#include "hamulce.h"
#include "utilities/Globals.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {

// reading TFV4aM::CP (the FV4a's control reservoir, private, no getter: GetCP() returns the time chamber)
// through the standard's explicit instantiation exemption from access checking; nothing is changed
template <typename Tag, typename Tag::type Member>
struct member_access {
	friend typename Tag::type get_member(Tag) { return Member; }
};
struct fv4a_control_reservoir {
	using type = double TFV4aM::*;
	friend type get_member(fv4a_control_reservoir);
};
template struct member_access<fv4a_control_reservoir, &TFV4aM::CP>;

double const primary_update_rate{1.0 / 100.0}; // driver_mode::m_primaryupdaterate
double const easy_reaction_time{0.5}; // Driver.cpp EasyReactionTime
double const prepare_time{2.0}; // Driver.cpp PrepareTime
double const ai_nomovement{0.05}; // Driver.h EU07_AI_NOMOVEMENT

std::string strip_fiz(std::string Name) {
	if (Name.size() > 4) {
		auto ext{Name.substr(Name.size() - 4)};
		for (auto &c : ext) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (ext == ".fiz") {
			Name.resize(Name.size() - 4);
		}
	}
	return Name;
}

std::string fixed(double Value, int Digits) {
	char buffer[64];
	std::snprintf(buffer, sizeof(buffer), "%.*f", Digits, Value);
	return buffer;
}

} // namespace

train::train(harness_options Options, track_conditions Track, event_sink Events) :
    m_options(std::move(Options)), m_track(Track), m_events(std::move(Events)) {
	Global.random_seed = m_options.seed;
	Global.random_engine.seed(m_options.seed);
	Global.local_random_engine.seed(m_options.seed);
}

void train::event(std::string const &Text) const {
	if (m_events) {
		m_events((m_preparing ? "prep t=" : "t=") + fixed(m_time, 2) + " " + Text);
	}
}

// TDynamicObject::Init(), the parts that set up the mover, in their order
void train::add_vehicle(std::string const &Folder, std::string const &Fiz, bool Flipped) {
	auto vehicle_ptr{std::make_unique<vehicle>()};
	auto &v{*vehicle_ptr};
	auto const index{m_vehicles.size()};
	v.iDirection = Flipped ? 0 : 1;
	v.name = strip_fiz(Fiz) + "#" + std::to_string(index);
	// the first vehicle is the driven one: "headdriver" (cab 1) or, turned, "reardriver" (cab -1); the rest "nobody"
	int const cab{index == 0 ? (Flipped ? -1 : 1) : 0};
	double const velocity{0.0}; // placed at a stand; the start speed is given after the hot start, see main.cpp
	v.mover = std::make_unique<TMoverParameters>(v.iDirection ? velocity : -velocity, strip_fiz(Fiz), v.name, cab);
	v.MoverParameters = v.mover.get();
	auto &mv{*v.mover};
	// paths::dynamic + BaseDir + "/"; the definition file is TypeName + ".fiz" in it, includes relative to it
	if (!mv.LoadFIZ(m_options.dynamic_dir + "/" + Folder + "/")) {
		throw std::runtime_error("MaSzyna failed to load " + Folder + "/" + Fiz + " (ConversionError " + std::to_string(ConversionError) + ")");
	}
	mv.AssignLoad("", 0.f);
	mv.ComputeMass();
	// Init passes driveractive = (fVel != 0): a train placed in motion is "ready to depart" (pipes charged,
	// reservoirs filled), one at a stand is "braked". The hot start wants the ready state at any speed
	bool const driveractive{true};
	if (!mv.CheckLocomotiveParameters(driveractive, (velocity > 0 ? 1 : -1) * cab * (v.iDirection ? 1 : -1))) {
		throw std::runtime_error("Parameters mismatch: " + Folder + "/" + Fiz);
	}
	mv.MainCtrlPos = mv.MainCtrlNoPowerPos();
	mv.LocalBrakePosA = 0.0;
	// driveractive branch
	mv.BrakeCtrlPos = static_cast<int>(std::floor(mv.Handle->GetPos(cab == 0 ? bh_NP : bh_RP)));
	// (Init also applies the independent brake fully for a manned vehicle placed slower than 1 km/h but
	// moving: driveractive && fVel < 1.0. It doesn't happen for the hot start and isn't replicated)
	mv.BrakeLevelSet(mv.BrakeCtrlPos);
	// create_controller(): a driver in the first vehicle
	v.has_driver = index == 0;
	v.mech_inside = index == 0; // TTrain::Init() for the player's vehicle
	// pantographs of the 3D model (iAnimType[ANIM_PANTS]); without the model: one per current collector of
	// the .fiz, at most two (the update handles pantographs 0 and 1 only)
	if (mv.EnginePowerSource.SourceType == TPowerSource::CurrentCollector) {
		v.pantographs = static_cast<int>(std::min<long>(2, mv.EnginePowerSource.CollectorParameters.CollectorsNo));
	}
	// placement: vehicles touching (scenario offsets), the head's front at 0
	if (index == 0) {
		v.position = -0.5 * mv.Dim.L;
	}
	else {
		auto const &previous{*m_vehicles.back()};
		v.position = previous.position - 0.5 * previous.mover->Dim.L - 0.5 * mv.Dim.L;
	}
	mv.Loc = location(v);
	mv.ComputeConstans();
	m_vehicles.push_back(std::move(vehicle_ptr));
	m_watched.emplace_back();
}

// state_serializer::deserialize_endtrainset -> TDynamicObject::AttachNext for each pair, then the
// primary driver's DirectionInitial() / CheckVehicles()
void train::couple() {
	for (std::size_t i = 1; i < m_vehicles.size(); ++i) {
		auto &previous{*m_vehicles[i - 1]};
		auto &next{*m_vehicles[i]};
		auto const vehicleend{previous.iDirection};
		auto const othervehicleend{next.iDirection ^ 1};
		auto const &coupler{previous.mover->Couplers[vehicleend]};
		auto const &othercoupler{next.mover->Couplers[othervehicleend]};
		// the coupling a scenario gives: the coupler, and brake hose, control and main hose where both ends allow
		int const allowed{coupler.AllowedFlag & othercoupler.AllowedFlag};
		int const couplingtype{(allowed & coupling::coupler) | (allowed & (coupling::brakehose | coupling::control | coupling::mainhose))};
		previous.mover->Attach(vehicleend, othervehicleend, next.mover.get(), couplingtype, true, false);
		update_neighbours(previous);
		update_neighbours(next);
		if (coupler.type() != othercoupler.type()) {
			event("warning: coupler types differ between " + previous.name + " and " + next.name + ", MaSzyna would fit an adapter (not replicated)");
		}
		if (previous.mover->Neighbours[vehicleend].distance <= -0.001) {
			// overlapping at the start: the next one moved back (doesn't happen with touching vehicles)
			move(next, previous.mover->Neighbours[vehicleend].distance * next.DirectionGet());
			next.mover->Loc = location(next);
			update_neighbours(previous);
			update_neighbours(next);
		}
		if (m_options.verbose) {
			event("coupled " + previous.name + " end " + std::to_string(vehicleend) + " with " + next.name + " end " + std::to_string(othervehicleend) +
			      ": flags " + std::to_string(couplingtype) + " (allowed " + std::to_string(coupler.AllowedFlag) + "/" + std::to_string(othercoupler.AllowedFlag) + ")");
		}
	}
	// DirectionInitial(): automatic cab activation
	loco().mover->CabActivisationAuto();
	// CheckVehicles(): the primary driver's housekeeping on the vehicles not under its control
	for (auto &p : m_vehicles) {
		if (p.get() == &loco()) {
			continue;
		}
		bool controlled{false};
		for (auto const *q{p->Prev(coupling::control)}; q != nullptr; q = q->Prev(coupling::control)) {
			controlled |= q == &loco();
		}
		for (auto const *q{p->Next(coupling::control)}; q != nullptr; q = q->Next(coupling::control)) {
			controlled |= q == &loco();
		}
		if (!controlled) {
			p->mover->BatterySwitch(true);
			if (p->mover->HeatingPower > 0) {
				p->mover->HeatingAllow = true;
				p->mover->ConverterSwitch(true, range_t::local);
			}
		}
	}
}

TLocation train::location(vehicle const &Vehicle) const {
	// vPosition on a straight line along the world z axis rising at the grade; Loc = {-x, z, y}
	auto const angle{std::atan(m_track.grade)};
	return TLocation{0.0, Vehicle.position * std::cos(angle), Vehicle.position * std::sin(angle)};
}

// TDynamicObject::Move(): along the track by Distance in the vehicle's own frame
void train::move(vehicle &Vehicle, double Distance) {
	if (Distance != 0.0) {
		Vehicle.position += Distance * Vehicle.DirectionGet();
	}
}

// TDynamicObject::update_neighbours()
void train::update_neighbours(vehicle &Vehicle) {
	auto &mv{*Vehicle.mover};
	for (int end = end::front; end <= end::rear; ++end) {
		auto &neighbour{mv.Neighbours[end]};
		auto const &coupler{mv.Couplers[end]};
		if (coupler.Connected != nullptr && neighbour.vehicle != nullptr) {
			neighbour.distance = TMoverParameters::CouplerDist(&mv, coupler.Connected);
			auto const &othercoupler{neighbour.vehicle->MoverParameters->Couplers[neighbour.vehicle_end]};
			neighbour.distance -= coupler.adapter_length;
			neighbour.distance -= othercoupler.adapter_length;
		}
		else {
			// find_vehicle(): the nearest vehicle on the track beyond this end, within the scan range
			neighbour = neighbour_data();
			auto const scanrange{std::max(10.0, std::abs(mv.V)) + 40.0};
			auto const facing{(end == end::front ? 1 : -1) * Vehicle.DirectionGet()}; // world direction this end faces
			vehicle *found{nullptr};
			double founddistance{std::numeric_limits<double>::max()};
			for (auto &other : m_vehicles) {
				if (other.get() == &Vehicle || !other->enabled) {
					continue;
				}
				auto const offset{(other->position - Vehicle.position) * facing};
				if (offset <= 0.0) {
					continue;
				}
				auto const gap{offset - 0.5 * (mv.Dim.L + other->mover->Dim.L)};
				if (gap < scanrange && gap < founddistance) {
					found = other.get();
					founddistance = gap;
				}
			}
			if (found == nullptr) {
				continue;
			}
			neighbour.vehicle = found;
			// the end of the found vehicle facing this one
			neighbour.vehicle_end = found->DirectionGet() == -facing ? end::front : end::rear;
			neighbour.distance = static_cast<float>(TMoverParameters::CouplerDist(&mv, found->mover.get()));
			auto const &othercoupler{found->mover->Couplers[neighbour.vehicle_end]};
			neighbour.distance -= coupler.adapter_length;
			neighbour.distance -= othercoupler.adapter_length;
		}
	}
}

// ts as TDynamicObject::Update() sets it: straight (both axles at the same angle: R 0), Len 1, the grade
// from the vehicle's front vector, no cant
void train::track_shape(vehicle &Vehicle) const {
	auto const angle{std::atan(m_track.grade)};
	auto const vfront_y{Vehicle.DirectionGet() * std::sin(angle)};
	Vehicle.ts.R = 0.0;
	Vehicle.ts.Len = 1.0;
	Vehicle.ts.dHtrack = -vfront_y;
	Vehicle.ts.dHrail = 0.0;
}

// TDynamicObject::UpdateForce()
void train::update_force(vehicle &Vehicle, double Dt) {
	if (!Vehicle.enabled) {
		return;
	}
	if (Dt > 0) {
		auto &mv{*Vehicle.mover};
		if (m_options.keep_awake && mv.CabActive == 0) {
			// ComputeTotalForce() switches the physics of a vehicle off when it has no active cab, stands, and so
			// do its neighbours, unless something switched within the last 5 s (LastSwitchingTime). Holding that
			// timer just under 5 s keeps every vehicle computed; it is read elsewhere only for the main switch,
			// compressor and diesel start delays of powered vehicles (all shorter), which don't apply to cars
			mv.LastSwitchingTime = std::min(mv.LastSwitchingTime, 4.9);
		}
		mv.ComputeTotalForce(Dt);
		if (Vehicle.has_driver) {
			// FTrain = (traction after the slip limit + coupler forces) / FT_factor + gravity, see ComputeTotalForce
			auto const ftfactor{(mv.EngineType == TEngineType::ElectricInductionMotor && mv.InvertersRatio > 0.0) ? 1.0 / mv.InvertersRatio : 1.0};
			Vehicle.traction_force = (mv.FTrain - mv.TotalMassxg * mv.RunningShape.dHtrack) * ftfactor - mv.Couplers[0].CForce - mv.Couplers[1].CForce;
		}
	}
}

// TDynamicObject::FastUpdate()
void train::fast_update(vehicle &Vehicle, double Dt) {
	if (Dt == 0.0) {
		return;
	}
	auto &mv{*Vehicle.mover};
	if (!mv.PhysicActivation || !Vehicle.enabled) {
		return;
	}
	auto const l{location(Vehicle)};
	TRotation const r{0.0, 0.0, Vehicle.DirectionGet() > 0 ? 0.0 : M_PI};
	auto const distance{mv.FastComputeMovement(Dt, Vehicle.ts, Vehicle.tp, l, r)};
	mv.dMoveLen += distance; // FastMove()
}

// TDynamicObject::GetEPP(): length weighted mean brake pipe pressure along the brake hoses
double train::get_epp(vehicle &Vehicle) const {
	double eq{0.0};
	double am{0.0};
	for (int pass = 0; pass < 2; ++pass) {
		vehicle *temp{&Vehicle};
		int coupler_nr{pass};
		for (int i = 0; i < 300 && temp != nullptr; ++i) {
			eq += temp->mover->PipePress * temp->mover->Dim.L;
			am += temp->mover->Dim.L;
			if ((temp->mover->Couplers[coupler_nr].CouplingFlag & coupling::brakehose) != coupling::brakehose) {
				break;
			}
			if (coupler_nr == 0) {
				if (temp->PrevConnected() == nullptr) {
					break;
				}
				if (temp->PrevConnectedNo() == end::front) {
					coupler_nr = 1 - coupler_nr;
				}
				temp = temp->PrevConnected();
			}
			else {
				if (temp->NextConnected() == nullptr) {
					break;
				}
				if (temp->NextConnectedNo() == end::rear) {
					coupler_nr = 1 - coupler_nr;
				}
				temp = temp->NextConnected();
			}
		}
	}
	eq -= Vehicle.mover->PipePress * Vehicle.mover->Dim.L;
	am -= Vehicle.mover->Dim.L;
	return eq / am;
}

// TDynamicObject::Update(), the parts which affect the mover, in their order
void train::update(vehicle &Vehicle, double Dt, double Dt1) {
	if (Dt1 == 0.0) {
		return;
	}
	auto &mv{*Vehicle.mover};
	if (!mv.PhysicActivation && !Vehicle.mech_inside) {
		return;
	}
	if (!Vehicle.enabled) {
		return;
	}
	track_shape(Vehicle);
	Vehicle.tp.Width = 1.435; // TTrack::fTrackWidth default
	Vehicle.tp.friction = m_track.friction * Global.fFriction * Global.FrictionWeatherFactor;
	Vehicle.tp.CategoryFlag = 1 & 15; // rail track
	Vehicle.tp.DamageFlag = 0;
	Vehicle.tp.QualityFlag = 20; // TTrack::iQualityFlag default
	mv.InsideConsist = mv.Couplers[0].CouplingFlag != coupling::faux && mv.Couplers[1].CouplingFlag != coupling::faux;
	// voltage of the overhead line
	if (mv.EnginePowerSource.SourceType == TPowerSource::CurrentCollector) {
		Vehicle.traction.TractionVoltage = std::max(std::abs(mv.PantRearVolt), std::abs(mv.PantFrontVolt));
		if (Vehicle.traction.TractionVoltage > 0.0) {
			Vehicle.no_volt_time = 0;
		}
		else {
			Vehicle.no_volt_time += Dt1;
			if (Vehicle.no_volt_time <= 0.2) {
				Vehicle.traction.TractionVoltage = mv.PantographVoltage;
			}
		}
	}
	else {
		Vehicle.traction.TractionVoltage = 0.95 * mv.EnginePowerSource.MaxVoltage;
	}
	Vehicle.traction.TractionFreq = 0;
	Vehicle.traction.TractionMaxCurrent = 7500;
	Vehicle.traction.TractionResistivity = 0.3;
	mv.PantographVoltage = Vehicle.traction.TractionVoltage;
	Vehicle.tp.Velmax = -1.0; // TTrack::fVelocity default (used by the AI only)
	if (Vehicle.has_driver) {
		// primary driver
		mv.EqvtPipePress = get_epp(Vehicle);
		// (the diesel electronic controller and induction motor blending blocks aren't replicated: main.cpp
		// refuses such locos)
		driver_update(Vehicle, Dt1); // Mechanik->Update()
	}
	// "fragment z EXE Kursa": no low voltage, no main switch
	if (mv.Mains) {
		if (!(mv.Power24vIsAvailable || mv.Power110vIsAvailable) && mv.EngineType != TEngineType::DieselEngine && mv.EngineType != TEngineType::WheelsDriven) {
			if (mv.MainSwitch(false, mv.TrainType == dt_EZT ? range_t::unit : range_t::local)) {
				mv.EventFlag = true;
			}
		}
	}
	auto const l{location(Vehicle)};
	TRotation const r{0.0, 0.0, Vehicle.DirectionGet() > 0 ? 0.0 : M_PI};
	auto const dDOMoveLen{mv.dMoveLen + mv.ComputeMovement(Dt, Dt1, Vehicle.ts, Vehicle.tp, Vehicle.traction, l, r)};
	move(Vehicle, dDOMoveLen);
	mv.dMoveLen = 0; // ResetdMoveLen()
	// pantographs. The game raises the 3D model's pantograph arms (at a speed set by the pantograph tank
	// pressure, only with low voltage power) and takes the wire's voltage while an arm touches the wire.
	// Here an arm is on the wire as long as it would be rising or held up, the wire at the line voltage
	if (Vehicle.pantographs > 0) {
		for (int i = 0; i < Vehicle.pantographs; ++i) {
			auto const &pantograph{mv.Pantographs[i == 0 ? end::front : end::rear]};
			// the voltage from where the arm was (MaSzyna: PantDiff of the previous frame)...
			(i == 0 ? mv.PantFrontVolt : mv.PantRearVolt) = pantograph.is_active && Vehicle.pantograph_on_wire[i] ? m_track.line_voltage : 0.0;
			// ...then the arm moves
			auto pantspeedfactor{mv.PantPress > (mv.TrainType == dt_EZT ? 2.45 : 3.45) ? 0.015 * mv.PantPress * Dt1 : 0.0};
			if (!(mv.Power24vIsAvailable || mv.Power110vIsAvailable)) {
				pantspeedfactor = 0.0;
			}
			auto const raising{pantspeedfactor > 0.0 && mv.Pantographs[i].is_active};
			if (raising != Vehicle.pantograph_on_wire[i] && m_options.verbose) {
				event(Vehicle.name + " pantograph " + std::to_string(i) + (raising ? " on the wire" : " off the wire"));
			}
			Vehicle.pantograph_on_wire[i] = raising;
		}
	}
	if (mv.EnginePowerSource.SourceType == TPowerSource::CurrentCollector && mv.EnginePowerSource.CollectorParameters.FakePower) {
		mv.PantRearVolt = 0.95 * mv.EnginePowerSource.MaxVoltage;
		mv.PantFrontVolt = 0.95 * mv.EnginePowerSource.MaxVoltage;
	}
	if (mv.InactiveCabPantsCheck) {
		// pants_up()
		bool anyup{false};
		for (auto &p : m_vehicles) {
			for (auto const &item : p->mover->Pantographs) {
				anyup |= item.is_active;
			}
		}
		if (anyup) {
			for (auto &p : m_vehicles) {
				p->mover->OperatePantographValve(end::front, operation_t::enable, range_t::local);
				p->mover->OperatePantographValve(end::rear, operation_t::enable, range_t::local);
			}
		}
		mv.InactiveCabPantsCheck = false;
	}
}

// vehicle_table::update()
void train::frame() {
	// driver_mode::update(): the frame split into steps of at most 0.01 s
	auto const deltatime{m_options.frame_dt};
	int updatecount{1};
	if (deltatime > primary_update_rate) {
		updatecount = static_cast<int>(std::ceil(deltatime / primary_update_rate));
	}
	auto const stepdeltatime{deltatime / updatecount};
	auto const state_update{[this](double Deltatime, int Iterationcount) {
		if (Deltatime == 0.0) {
			return;
		}
		for (auto &v : m_vehicles) {
			if (!v->enabled) {
				continue;
			}
			// (update_traction: the wire search; the wire is everywhere here, see update())
			v->mover->ComputeConstans();
			update_neighbours(*v);
		}
		if (Iterationcount > 1) {
			for (int iteration = 0; iteration < Iterationcount - 1; ++iteration) {
				for (auto &v : m_vehicles) {
					update_force(*v, Deltatime);
				}
				for (auto &v : m_vehicles) {
					fast_update(*v, Deltatime);
				}
			}
		}
		for (auto &v : m_vehicles) {
			update_force(*v, Deltatime);
		}
		auto const totaltime{Deltatime * Iterationcount};
		for (auto &v : m_vehicles) {
			update(*v, Deltatime, totaltime);
		}
	}};
	if (Global.FullPhysics) {
		while (updatecount >= 5) {
			state_update(stepdeltatime, 5);
			updatecount -= 5;
		}
		if (updatecount) {
			state_update(stepdeltatime, updatecount);
		}
	}
	else {
		state_update(stepdeltatime, updatecount);
	}
	m_time += deltatime;
	watch_events();
}

// what TController::Update() does for a train a player drives: the reaction time gate, then the consist
// check, in which the virtual trainman (Global.AITrainman) releases overcharged brakes at a stand. During
// the hot start the AI's preparation (PrepareEngine) and its alerter/cab signal resets are done as well
void train::driver_update(vehicle &Vehicle, double Dt) {
	m_reaction_timer += Dt;
	auto const reactiontime{std::min(m_reaction_time, 2.0)};
	if (m_reaction_timer < reactiontime) {
		return;
	}
	m_reaction_timer -= reactiontime;
	trainman_check();
	if (m_preparing) {
		security_reset_as_ai();
		prepare_engine();
	}
}

// TController::determine_consist_state(), the releaser part, for AIControllFlag false
void train::trainman_check() {
	if (!m_options.trainman || !Global.AITrainman) {
		return;
	}
	auto &occupied{*loco().mover};
	bool ready{true};
	for (auto *p{&loco()}; p != nullptr; p = p->Next()) {
		auto &mv{*p->mover};
		auto const bp{std::max(0.0, mv.BrakePress - (mv.SpeedCtrlUnit.Parking ? mv.MaxBrakePress[0] * mv.StopBrakeDecc : 0.0))};
		if (!ready) {
			continue;
		}
		auto const status{mv.Hamulec->GetBrakeStatus()};
		auto const isbraking{(status & (b_hld | b_on)) != 0};
		if (mv.Vel < 1.0 ? (isbraking || bp > 0.4) : ((isbraking && bp > 0.4) || mv.Fb > 10000.0)) {
			ready = false;
		}
		double const brakectrlposition{0.0}; // TController::BrakeCtrlPosition: gbh_RP for a player's driver
		if (bp >= 0.4 && occupied.Vel < ai_nomovement && occupied.TrainType != dt_EZT && occupied.TrainType != dt_DMU && brakectrlposition == 0.0 /*gbh_RP*/
		    && (status & b_dmg) == 0 && mv.PipePress - occupied.Handle->GetRP() > -0.1 && mv.Hamulec->GetCRP() > mv.PipePress + 0.12) {
			if ((status & b_rls) == 0) {
				++m_trainman_releases;
				event("virtual trainman releases " + p->name + " (cylinder " + fixed(bp, 2) + " bar, control chamber " + fixed(mv.Hamulec->GetCRP(), 3) + " > pipe " +
				      fixed(mv.PipePress, 3) + " + 0.12)");
			}
			mv.BrakeReleaser(1);
		}
		if (bp < 0.1 && (status & b_dmg) == 0 && mv.Hamulec->GetCRP() < mv.PipePress - 0.1) {
			mv.BrakeReleaser(0);
		}
	}
}

// TController::control_security_system() as the AI does it (driver_hint securitysystemreset/shpsystemreset)
void train::security_reset_as_ai() {
	auto &mv{*loco().mover};
	if (mv.SecuritySystem.is_cabsignal_blinking() && mv.SecuritySystem.has_separate_acknowledge()) {
		mv.SecuritySystem.cabsignal_reset();
	}
	if (mv.SecuritySystem.is_blinking()) {
		mv.SecuritySystemReset();
	}
}

// TController::PrepareEngine() with the actions of application/driverhints.cpp an AI driver takes
// (mvOccupied = mvControlling = mvPantographUnit = the loco), and the pantograph air source switch of
// control_pantographs()
void train::prepare_engine() {
	auto &mv{*loco().mover};
	mv.BatterySwitch(true);
	if (mv.Power24vIsAvailable) {
		mv.CabActivisation();
	}
	mv.Radio = true;
	if (mv.EnginePowerSource.SourceType == TPowerSource::CurrentCollector && mv.EnginePowerSource.CollectorParameters.CollectorsNo > 0) {
		auto const sufficientpantographpressure{(mv.TrainType == dt_EZT ? 2.5 : 3.5) + 0.1};
		if (mv.PantPress < sufficientpantographpressure) {
			if (!mv.PantAutoValve) {
				mv.bPantKurek3 = false;
			}
			mv.PantCompFlag = true;
		}
		else {
			if (!mv.bPantKurek3 || mv.PantPress <= mv.ScndPipePress) {
				mv.PantCompFlag = false;
			}
			// control_pantographs(): the pantograph tank back on the main reservoir once that one has pressure
			if (!mv.PantAutoValve && mv.ScndPipePress > 4.3) {
				mv.bPantKurek3 = true;
			}
		}
		// no overhead line events here (fOverhead2 == -1, iOverheadDown == 0)
		mv.OperatePantographsValve(operation_t::enable);
		mv.OperatePantographValve(end::front, operation_t::enable);
		mv.OperatePantographValve(end::rear, operation_t::enable);
	}
	auto const ispoweravailable{mv.EnginePowerSource.SourceType != TPowerSource::CurrentCollector ||
	                            std::max(mv.GetTrainsetHighVoltage(), mv.PantographVoltage) > mv.EnginePowerSource.CollectorParameters.MinV};
	if (!ispoweravailable) {
		return;
	}
	// PrepareDirection(): at a stand the direction of the occupied cab, then OrderDirectionChange() ->
	// TController::DirectionForward(true): the reverser forward (the scenario sets it again at t=0)
	if (mv.Vel < 0.5 && mv.CabActive != 0) {
		mv.MainCtrlPos = std::min(mv.MainCtrlPos, mv.MainCtrlMaxDirChangePos);
		while (mv.DirActive <= 0 && mv.DirectionForward()) {
		}
	}
	auto const converteroverload{mv.ConvOvldFlag};
	auto const linebreakeropen{mv.Power > 0.01 && !mv.Mains};
	auto const converterpresent{mv.ConverterStart != start_t::disabled};
	auto const converterenabled{(mv.ConverterAllow || mv.ConverterStart == start_t::automatic) && mv.ConverterAllowLocal};
	auto const compressorpresent{mv.CompressorSpeed > 0.0};
	if (converteroverload) {
		mv.CompressorSwitch(false);
		mv.ConverterSwitch(false);
		mv.RelayReset(relay_t::primaryconverteroverload);
	}
	if (linebreakeropen) {
		// ZeroSpeed()
		while (!mv.IsScndCtrlNoPowerPos() && mv.DecScndCtrl(1)) {
		}
		while (!mv.IsMainCtrlNoPowerPos() && mv.DecMainCtrl(1)) {
		}
		mv.RelayReset(relay_t::maincircuitground);
		mv.RelayReset(relay_t::tractionnmotoroverload);
		mv.MainSwitch(true);
	}
	else {
		if (!converteroverload) {
			if (converterpresent) {
				mv.ConverterSwitch(true);
			}
			if (compressorpresent && converterenabled) {
				mv.CompressorSwitch(true);
			}
			if (mv.MotorBlowers[end::front].speed != 0) {
				mv.MotorBlowersSwitchOff(false, end::front);
				mv.MotorBlowersSwitch(true, end::front);
			}
			if (mv.MotorBlowers[end::rear].speed != 0) {
				mv.MotorBlowersSwitchOff(false, end::rear);
				mv.MotorBlowersSwitch(true, end::rear);
			}
		}
		if (mv.fBrakeCtrlPos != mv.Handle->GetPos(bh_RP)) {
			mv.BrakeLevelSet(mv.Handle->GetPos(bh_RP));
		}
		mv.SpringBrakeActivate(mv.SpringBrake.Activate);
	}
}

double train::hot_start() {
	m_preparing = true;
	m_reaction_time = prepare_time; // PrepareEngine(): ReactionTime = PrepareTime at a stand
	m_time = 0.0;
	double settled_since{-1.0};
	while (m_time < m_options.preroll_max) {
		frame();
		auto &mv{*loco().mover};
		bool settled{m_time > 30.0};
		settled = settled && (mv.Power < 0.01 || mv.Mains);
		settled = settled && (mv.CompressorSpeed <= 0.0 || (mv.CompressorAllow || mv.CompressorStart == start_t::automatic));
		auto const pipe{mv.PipePress};
		auto const mainpipe{mv.ScndPipePress};
		for (auto &v : m_vehicles) {
			settled = settled && std::abs(v->mover->PipePress - pipe) < 0.005;
			settled = settled && v->mover->BrakePress < 0.01;
			if ((v->mover->Couplers[0].CouplingFlag | v->mover->Couplers[1].CouplingFlag) & coupling::mainhose) {
				settled = settled && std::abs(v->mover->ScndPipePress - mainpipe) < 0.05;
			}
		}
		settled = settled && !mv.SecuritySystem.is_blinking();
		if (settled && settled_since < 0.0) {
			settled_since = m_time;
		}
		if (!settled) {
			settled_since = -1.0;
		}
		// done once settled and the compressor has just been stopped by its governor (main reservoir full)
		if (settled_since >= 0.0 && (mv.CompressorSpeed <= 0.0 || (!mv.CompressorFlag && mv.Compressor >= mv.MaxCompressor - 0.05))) {
			break;
		}
	}
	auto const used{m_time};
	if (used >= m_options.preroll_max) {
		event("warning: the hot start didn't settle within " + fixed(m_options.preroll_max, 0) + " s, starting anyway");
	}
	security_reset_as_ai();
	m_preparing = false;
	m_reaction_time = easy_reaction_time;
	m_reaction_timer = 0.0;
	return used;
}

void train::set_speed(double Kmh) {
	for (auto &v : m_vehicles) {
		auto &mv{*v->mover};
		mv.V = Kmh / 3.6 * v->DirectionGet();
		mv.Vel = std::abs(Kmh);
		mv.AccS = 0.0;
		mv.AccSVBased = 0.0;
		mv.nrot = mv.V / (M_PI * mv.WheelDiameter); // ComputeTotalForce recomputes it (v2n) before use
	}
}

// TTrain::OnCommand_reverserforward / reverserbackward / reverserneutral
bool train::set_direction(int Direction) {
	auto &mv{*loco().mover};
	if (!mv.EIMDirectionChangeAllow()) {
		return false;
	}
	while (mv.DirActive < Direction && mv.DirectionForward()) {
	}
	while (mv.DirActive > Direction && mv.DirectionBackward()) {
	}
	return mv.DirActive == Direction;
}

// TTrain::OnCommand_mastercontrollerincrease / decrease
bool train::main_controller_step(int Towards) {
	auto &mv{*loco().mover};
	return Towards > 0 ? mv.IncMainCtrl(1) : mv.DecMainCtrl(1);
}

// TTrain::OnCommand_secondcontrollerincrease / decrease
bool train::second_controller_step(int Towards) {
	auto &mv{*loco().mover};
	return Towards > 0 ? mv.IncScndCtrl(1) : mv.DecScndCtrl(1);
}

// TTrain::set_train_brake()
void train::set_train_brake(double Position) {
	loco().mover->BrakeLevelSet(Position);
}

// TTrain::OnCommand_independentbrakeset (LocalBrakePosA 0..1)
void train::set_local_brake(double Position) {
	loco().mover->LocalBrakePosA = std::clamp(Position / LocalBrakePosNo, 0.0, 1.0);
}

// TTrain::OnCommand_alerteracknowledge: press, then release
void train::acknowledge_vigilance() {
	auto &mv{*loco().mover};
	if (mv.TrainType == dt_EZT || mv.DirActive != 0) {
		mv.SecuritySystem.acknowledge_press();
		mv.SecuritySystem.acknowledge_release();
	}
	else {
		event("vigilance acknowledge ignored: the reverser is at 0 (as TTrain does)");
	}
}

double train::driver_valve_chamber() const {
	auto const *handle{dynamic_cast<TFV4aM const *>(m_vehicles.front()->mover->Handle.get())};
	if (handle == nullptr) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return handle->*get_member(fv4a_control_reservoir{});
}

void train::watch_events() {
	// the first pass only records the state
	auto const say{[this](std::string const &Text) {
		if (m_watched_ready) {
			event(Text);
		}
	}};
	for (std::size_t i = 0; i < m_vehicles.size(); ++i) {
		auto &v{*m_vehicles[i]};
		auto &mv{*v.mover};
		auto &w{m_watched[i]};
		auto const describe{[&]() {
			return " (v " + fixed(mv.Vel, 1) + " km/h, main " + std::to_string(mv.MainCtrlPos) + "/" + std::to_string(mv.MainCtrlActualPos) + ", second " +
			       std::to_string(mv.ScndCtrlPos) + "/" + std::to_string(mv.ScndCtrlActualPos) + ", Im " + fixed(mv.Im, 0) + " A, Imax " + fixed(mv.Imax, 0) + " A)";
		}};
		if (mv.Power > 0.01) {
			if (mv.Mains != w.mains) {
				say(v.name + (mv.Mains ? " main switch closed" : " main switch opened") + describe());
			}
			if (mv.FuseFlag != w.fuse) {
				say(v.name + (mv.FuseFlag ? " motor overload relay tripped (FuseFlag)" : " motor overload relay reset") + describe());
			}
			if (mv.StLinFlag != w.stlin && m_options.verbose) {
				say(v.name + (mv.StLinFlag ? " line contactors closed" : " line contactors open") + describe());
			}
			if (mv.ConvOvldFlag != w.convovld) {
				say(v.name + (mv.ConvOvldFlag ? " converter overload relay tripped" : " converter overload relay reset"));
			}
			if (mv.CompressorFlag != w.compressor && m_options.verbose) {
				say(v.name + (mv.CompressorFlag ? " compressor on" : " compressor off") + " at " + fixed(mv.Compressor, 3) + " bar");
			}
		}
		if (mv.SlippingWheels != w.slipping) {
			say(v.name + (mv.SlippingWheels ? " wheels slipping" : " wheels gripping") + describe());
		}
		if (mv.DamageFlag != w.damage) {
			say(v.name + " damage flags " + std::to_string(w.damage) + " -> " + std::to_string(mv.DamageFlag));
		}
		for (int end = 0; end < 2; ++end) {
			if (mv.Couplers[end].CouplingFlag != w.coupling[end]) {
				say(v.name + " coupler " + std::to_string(end) + " flags " + std::to_string(w.coupling[end]) + " -> " + std::to_string(mv.Couplers[end].CouplingFlag));
			}
			w.coupling[end] = mv.Couplers[end].CouplingFlag;
		}
		auto const releaser{(mv.Hamulec->GetBrakeStatus() & b_rls) != 0};
		if (releaser != w.releaser && m_options.verbose) {
			say(v.name + (releaser ? " brake releaser on" : " brake releaser off"));
		}
		if (v.has_driver) {
			auto const alert{mv.SecuritySystem.is_blinking()};
			auto const braking{mv.SecuritySystem.is_braking()};
			if (alert != w.alert && (!m_preparing || m_options.verbose)) {
				say(v.name + (alert ? " vigilance/cab signal alert lit" : " vigilance/cab signal alert cleared"));
			}
			if (braking != w.braking) {
				say(v.name + (braking ? " vigilance/cab signal EMERGENCY BRAKING" : " vigilance/cab signal braking ended"));
			}
			w.alert = alert;
			w.braking = braking;
		}
		w.mains = mv.Mains;
		w.fuse = mv.FuseFlag;
		w.stlin = mv.StLinFlag;
		w.slipping = mv.SlippingWheels;
		w.convovld = mv.ConvOvldFlag;
		w.compressor = mv.CompressorFlag;
		w.damage = mv.DamageFlag;
		w.releaser = releaser;
	}
	m_watched_ready = true;
}
