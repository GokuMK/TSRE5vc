// This file is part of TSRE5's train comparison tool (task driving 10): it
// stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
// 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
// is: this Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

#include "scenario.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace scenario {

namespace {

// a JSON value: null, bool, number, string, array, object
struct json;
using json_array = std::vector<json>;
using json_object = std::map<std::string, json>;

struct json {
	std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<json_array>, std::shared_ptr<json_object>> value{nullptr};

	bool is_number() const { return std::holds_alternative<double>(value); }
	bool is_string() const { return std::holds_alternative<std::string>(value); }
	bool is_bool() const { return std::holds_alternative<bool>(value); }
	bool is_array() const { return std::holds_alternative<std::shared_ptr<json_array>>(value); }
	bool is_object() const { return std::holds_alternative<std::shared_ptr<json_object>>(value); }

	double number(std::string const &What) const {
		if (!is_number()) {
			throw std::runtime_error(What + ": a number expected");
		}
		return std::get<double>(value);
	}
	std::string const &string(std::string const &What) const {
		if (!is_string()) {
			throw std::runtime_error(What + ": a string expected");
		}
		return std::get<std::string>(value);
	}
	json_array const &array(std::string const &What) const {
		if (!is_array()) {
			throw std::runtime_error(What + ": an array expected");
		}
		return *std::get<std::shared_ptr<json_array>>(value);
	}
	json_object const &object(std::string const &What) const {
		if (!is_object()) {
			throw std::runtime_error(What + ": an object expected");
		}
		return *std::get<std::shared_ptr<json_object>>(value);
	}
};

class reader {
public:
	explicit reader(std::string Text) : m_text(std::move(Text)) {}

	json document() {
		auto result{value()};
		skip_space();
		if (m_pos != m_text.size()) {
			fail("trailing characters");
		}
		return result;
	}

private:
	[[noreturn]] void fail(std::string const &What) const {
		std::size_t line{1};
		for (std::size_t i = 0; i < m_pos && i < m_text.size(); ++i) {
			line += m_text[i] == '\n' ? 1 : 0;
		}
		throw std::runtime_error("JSON: " + What + " (line " + std::to_string(line) + ")");
	}

	void skip_space() {
		while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) {
			++m_pos;
		}
	}

	bool take(char C) {
		skip_space();
		if (m_pos < m_text.size() && m_text[m_pos] == C) {
			++m_pos;
			return true;
		}
		return false;
	}

	void expect(char C) {
		if (!take(C)) {
			fail(std::string("'") + C + "' expected");
		}
	}

	json value() {
		skip_space();
		if (m_pos >= m_text.size()) {
			fail("unexpected end");
		}
		auto const c{m_text[m_pos]};
		json result;
		if (c == '{') {
			++m_pos;
			auto object{std::make_shared<json_object>()};
			if (!take('}')) {
				do {
					skip_space();
					auto const key{string_literal()};
					expect(':');
					(*object)[key] = value();
				} while (take(','));
				expect('}');
			}
			result.value = object;
		}
		else if (c == '[') {
			++m_pos;
			auto array{std::make_shared<json_array>()};
			if (!take(']')) {
				do {
					array->push_back(value());
				} while (take(','));
				expect(']');
			}
			result.value = array;
		}
		else if (c == '"') {
			result.value = string_literal();
		}
		else if (m_text.compare(m_pos, 4, "true") == 0) {
			m_pos += 4;
			result.value = true;
		}
		else if (m_text.compare(m_pos, 5, "false") == 0) {
			m_pos += 5;
			result.value = false;
		}
		else if (m_text.compare(m_pos, 4, "null") == 0) {
			m_pos += 4;
		}
		else {
			char *end{nullptr};
			auto const number{std::strtod(m_text.c_str() + m_pos, &end)};
			if (end == m_text.c_str() + m_pos) {
				fail("a value expected");
			}
			m_pos = static_cast<std::size_t>(end - m_text.c_str());
			result.value = number;
		}
		return result;
	}

	std::string string_literal() {
		if (m_pos >= m_text.size() || m_text[m_pos] != '"') {
			fail("a string expected");
		}
		++m_pos;
		std::string result;
		while (m_pos < m_text.size() && m_text[m_pos] != '"') {
			auto c{m_text[m_pos++]};
			if (c == '\\' && m_pos < m_text.size()) {
				auto const e{m_text[m_pos++]};
				switch (e) {
				case 'n': c = '\n'; break;
				case 't': c = '\t'; break;
				case 'r': c = '\r'; break;
				case 'b': c = '\b'; break;
				case 'f': c = '\f'; break;
				case 'u': {
					// only what the scenarios could need: code points below 0x800, as UTF-8
					auto const code{std::strtol(m_text.substr(m_pos, 4).c_str(), nullptr, 16)};
					m_pos += 4;
					if (code < 0x80) {
						c = static_cast<char>(code);
					}
					else {
						result += static_cast<char>(0xC0 | (code >> 6));
						c = static_cast<char>(0x80 | (code & 0x3F));
					}
					break;
				}
				default: c = e; break;
				}
			}
			result += c;
		}
		if (m_pos >= m_text.size()) {
			fail("unterminated string");
		}
		++m_pos;
		return result;
	}

	std::string m_text;
	std::size_t m_pos{0};
};

json const *find(json_object const &Object, std::string const &Key) {
	auto const lookup{Object.find(Key)};
	return lookup != Object.end() ? &lookup->second : nullptr;
}

} // namespace

char const *to_string(command_kind Kind) {
	switch (Kind) {
	case command_kind::direction: return "direction";
	case command_kind::main_controller: return "mainController";
	case command_kind::second_controller: return "secondController";
	case command_kind::train_brake_position: return "trainBrakePosition";
	case command_kind::local_brake_position: return "localBrakePosition";
	}
	return "?";
}

definition load(std::string const &Path) {
	std::ifstream file(Path, std::ios::binary);
	if (!file) {
		throw std::runtime_error("can't open scenario " + Path);
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	auto const document{reader(buffer.str()).document()};
	auto const &root{document.object("scenario")};

	definition result;
	if (auto const *v{find(root, "name")}) { result.name = v->string("name"); }
	if (auto const *v{find(root, "description")}) { result.description = v->string("description"); }
	if (auto const *v{find(root, "lineVoltage")}) { result.line_voltage = v->number("lineVoltage"); }
	if (auto const *v{find(root, "track")}) {
		auto const &track{v->object("track")};
		if (auto const *t{find(track, "length")}) { result.track_length = t->number("track.length"); }
		if (auto const *t{find(track, "grade")}) { result.track_grade = t->number("track.grade"); }
		if (auto const *t{find(track, "friction")}) { result.track_friction = t->number("track.friction"); }
	}
	if (auto const *v{find(root, "consist")}) {
		for (auto const &entry : v->array("consist")) {
			auto const &item{entry.object("consist entry")};
			vehicle_entry vehicle;
			if (auto const *f{find(item, "folder")}) { vehicle.folder = f->string("consist.folder"); }
			if (auto const *f{find(item, "fiz")}) { vehicle.fiz = f->string("consist.fiz"); }
			if (auto const *f{find(item, "tsre")}) { vehicle.tsre = f->string("consist.tsre"); }
			if (auto const *f{find(item, "count")}) { vehicle.count = static_cast<int>(f->number("consist.count")); }
			if (auto const *f{find(item, "flipped")}) {
				vehicle.flipped = f->is_bool() ? std::get<bool>(f->value) : f->number("consist.flipped") != 0.0;
			}
			if (vehicle.folder.empty() || vehicle.fiz.empty()) {
				throw std::runtime_error("consist entry without folder or fiz");
			}
			if (vehicle.count < 1) {
				throw std::runtime_error("consist entry " + vehicle.fiz + " with count below 1");
			}
			result.consist.push_back(vehicle);
		}
	}
	if (result.consist.empty()) {
		throw std::runtime_error("empty consist");
	}
	if (auto const *v{find(root, "startSpeed")}) { result.start_speed = v->number("startSpeed"); }
	if (auto const *v{find(root, "acknowledge")}) { result.acknowledge = v->number("acknowledge"); }
	if (auto const *v{find(root, "duration")}) { result.duration = v->number("duration"); }
	if (auto const *v{find(root, "sample")}) { result.sample = v->number("sample"); }
	if (auto const *v{find(root, "commands")}) {
		std::map<std::string, command_kind> const kinds{
		    {"direction", command_kind::direction},
		    {"mainController", command_kind::main_controller},
		    {"secondController", command_kind::second_controller},
		    {"trainBrakePosition", command_kind::train_brake_position},
		    {"localBrakePosition", command_kind::local_brake_position},
		};
		for (auto const &entry : v->array("commands")) {
			auto const &item{entry.object("command")};
			command cmd;
			auto const *t{find(item, "t")};
			if (t == nullptr) {
				throw std::runtime_error("command without t");
			}
			cmd.t = t->number("command.t");
			int found{0};
			for (auto const &[key, kind] : kinds) {
				auto const *value{find(item, key)};
				if (value == nullptr) {
					continue;
				}
				++found;
				cmd.kind = kind;
				if (value->is_object()) {
					auto const &ramp{value->object(key)};
					auto const *to{find(ramp, "to")};
					auto const *every{find(ramp, "every")};
					if (to == nullptr || every == nullptr) {
						throw std::runtime_error(key + " ramp needs to and every");
					}
					cmd.value = to->number(key + ".to");
					cmd.every = every->number(key + ".every");
					if (*cmd.every <= 0.0) {
						throw std::runtime_error(key + ".every must be positive");
					}
				}
				else {
					cmd.value = value->number(key);
				}
			}
			if (found != 1) {
				throw std::runtime_error("a command at t=" + std::to_string(cmd.t) + " needs exactly one action");
			}
			result.commands.push_back(cmd);
		}
	}
	if (result.sample <= 0.0 || result.duration <= 0.0) {
		throw std::runtime_error("duration and sample must be positive");
	}
	if (result.acknowledge < 0.0) {
		throw std::runtime_error("acknowledge must be 0 (never) or positive");
	}
	return result;
}

} // namespace scenario
