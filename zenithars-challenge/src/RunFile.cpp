#include "RunFile.h"

#include "Stats.h"

#include "Events.h"
#include "Ledger.h"
#include "Rules.h"
#include "Sha256.h"

#include <filesystem>
#include <fstream>

namespace
{
	constexpr int kFormat = 1;

	// The seal shows a file was not edited by hand. It is not secret-proof: the key ships inside
	// the plugin, so a determined person can forge it. That is accepted for a for-fun challenge.
	constexpr std::string_view kSealKey = "zenithars-challenge/run-file/v1/3f9c1e7a52b84d06a1c7e0d49b6f8235"sv;

	// How many bytes the UTF-8 character starting at a_at takes, or 0 if what is there is not UTF-8.
	[[nodiscard]] std::size_t Utf8Length(std::string_view a_text, std::size_t a_at)
	{
		const auto    first = static_cast<unsigned char>(a_text[a_at]);
		std::size_t   length = 0;
		std::uint32_t code = 0;
		std::uint32_t least = 0;
		if (first >= 0xC2 && first <= 0xDF) {
			length = 2;
			code = first & 0x1Fu;
			least = 0x80;
		} else if (first >= 0xE0 && first <= 0xEF) {
			length = 3;
			code = first & 0x0Fu;
			least = 0x800;
		} else if (first >= 0xF0 && first <= 0xF4) {
			length = 4;
			code = first & 0x07u;
			least = 0x10000;
		} else {
			return 0;
		}
		if (a_at + length > a_text.size()) {
			return 0;
		}
		for (std::size_t i = 1; i < length; ++i) {
			const auto next = static_cast<unsigned char>(a_text[a_at + i]);
			if ((next & 0xC0u) != 0x80u) {
				return 0;
			}
			code = (code << 6) | (next & 0x3Fu);
		}
		if (code < least || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
			return 0;
		}
		return length;
	}

	// For a JSON string. The file has to be valid UTF-8 or the site cannot check its seal, and a
	// name from an old mod may be in a Windows code page instead: such a byte is written as an escape.
	[[nodiscard]] std::string Escape(std::string_view a_text)
	{
		std::string out;
		out.reserve(a_text.size() + 2);
		for (std::size_t at = 0; at < a_text.size(); ++at) {
			const auto ch = a_text[at];
			const auto byte = static_cast<unsigned char>(ch);
			switch (ch) {
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				if (byte < 0x20) {
					out += std::format("\\u{:04x}", static_cast<unsigned>(byte));
				} else if (byte < 0x80) {
					out.push_back(ch);
				} else if (const auto length = Utf8Length(a_text, at); length > 0) {
					out.append(a_text.substr(at, length));
					at += length - 1;
				} else {
					out += std::format("\\u{:04x}", static_cast<unsigned>(byte));
				}
				break;
			}
		}
		return out;
	}

	[[nodiscard]] std::string Quote(std::string_view a_text)
	{
		return std::format("\"{}\"", Escape(a_text));
	}

	// "Skyrim.esm|012EB7": stable across load orders. Items made while playing have no plugin.
	[[nodiscard]] std::string FormKey(RE::FormID a_form)
	{
		const auto form = a_form ? RE::TESForm::LookupByID(a_form) : nullptr;
		if (!form) {
			return std::format("?|{:08X}", a_form);
		}
		if (form->IsDynamicForm()) {
			return std::format("made-in-game|{:08X}", a_form);
		}
		const auto plugin = Rules::DefiningPlugin(form);
		return std::format("{}|{:06X}", plugin.empty() ? "?"sv : plugin, form->GetLocalFormID());
	}

	[[nodiscard]] std::string FormName(RE::FormID a_form)
	{
		const auto form = a_form ? RE::TESForm::LookupByID(a_form) : nullptr;
		return form ? Rules::ItemName(form) : std::string{ "(gone)" };
	}

	// A kind of creature. Some races have no name in the game ("GoblinRace" is all there is), so the
	// record's own ID stands in, without the word Race.
	[[nodiscard]] std::string KindName(RE::FormID a_race)
	{
		const auto race = a_race ? RE::TESForm::LookupByID<RE::TESRace>(a_race) : nullptr;
		if (!race) {
			return FormName(a_race);
		}
		if (const auto name = race->GetName(); name && name[0] != '\0') {
			return name;
		}
		std::string id = race->GetFormEditorID() ? race->GetFormEditorID() : "";
		if (id.empty()) {
			return FormName(a_race);
		}
		if (id.size() > 4 && id.ends_with("Race")) {
			id.erase(id.size() - 4);
		}
		return id;
	}

	[[nodiscard]] std::string_view RuleName(Rule a_rule) noexcept
	{
		switch (a_rule) {
		case Rule::kGear:
			return "gear"sv;
		case Rule::kFood:
			return "food"sv;
		case Rule::kPotion:
			return "potion"sv;
		case Rule::kSale:
			return "sale"sv;
		case Rule::kMagic:
			return "magic"sv;
		case Rule::kMissive:
			return "missive"sv;
		case Rule::kMaterial:
			return "material"sv;
		case Rule::kConsole:
			return "console"sv;
		default:
			return "other"sv;
		}
	}

	[[nodiscard]] std::string SafeFileName(std::string_view a_name)
	{
		std::string out;
		for (const auto ch : a_name) {
			const auto u = static_cast<unsigned char>(ch);
			out.push_back((std::isalnum(u) || ch == '-' || ch == '_' || ch == ' ') ? ch : '_');
		}
		while (!out.empty() && out.back() == ' ') {
			out.pop_back();
		}
		return out.empty() ? std::string{ "character" } : out;
	}

	// The load order as one number and one fingerprint, so the site can tell a stock LoreRim
	// install from a changed one without listing anyone's mods.
	void DescribePlugins(std::uint32_t& a_count, std::string& a_hash)
	{
		a_count = 0;
		std::string names;
		if (const auto data = RE::TESDataHandler::GetSingleton()) {
			const auto mods = data->GetLoadedMods();
			for (std::uint32_t i = 0; mods && i < data->GetLoadedModCount(); ++i) {
				if (const auto file = mods[i]) {
					names.append(file->GetFilename()).push_back('\n');
					++a_count;
				}
			}
			const auto lightMods = data->GetLoadedLightMods();
			for (std::uint32_t i = 0; lightMods && i < data->GetLoadedLightModCount(); ++i) {
				if (const auto file = lightMods[i]) {
					names.append(file->GetFilename()).push_back('\n');
					++a_count;
				}
			}
		}
		a_hash = Sha256::Hex(Sha256::Hash(names));
	}

	[[nodiscard]] std::string BuildBody()
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto snapshot = Ledger::Get().TakeSnapshot();
		const auto calendar = RE::Calendar::GetSingleton();
		const auto race = player ? player->GetRace() : nullptr;

		std::string body = "{";
		body += std::format("\"format\":{},", kFormat);
		body += std::format("\"mod\":{},", Quote(SKSE::GetPluginVersion().string(".")));
		body += std::format("\"written\":{},", Quote(std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()))));
		body += std::format("\"character\":{{\"name\":{},\"race\":{},\"level\":{}}},",
			Quote(player ? player->GetName() : ""), Quote(race ? race->GetName() : ""), player ? player->GetLevel() : 0);
		body += "\"mode\":\"oath\",";
		body += std::format("\"modeChosen\":{},", snapshot.mode == Mode::kUnset ? "false" : "true");
		body += std::format("\"options\":{{\"lootGems\":{},\"lootSoulGems\":{},\"coinPurseGems\":{},\"questRewards\":{},\"startingItems\":{},\"othersKills\":{}}},",
			(snapshot.options & Option::kLootGems) != 0, (snapshot.options & Option::kLootSoulGems) != 0, (snapshot.options & Option::kCoinPurseGems) != 0,
			(snapshot.options & Option::kQuestRewards) != 0, (snapshot.options & Option::kStartingItems) != 0, (snapshot.options & Option::kOthersKills) != 0);
		body += std::format("\"daysPassed\":{:.2f},", calendar ? calendar->GetDaysPassed() : 0.0F);
		// When the challenge was taken, and whether by a new character or one that already had a past.
		if (snapshot.joined != Joined::kUnknown) {
			body += std::format("\"taken\":{{\"day\":{:.2f},\"character\":{}}},", std::max(0.0F, snapshot.takenOnDay),
				Quote(snapshot.joined == Joined::kNew ? "new"sv : "existing"sv));
		}

		// What was made, by how.
		Ledger::MadeCounts totals{};
		std::string        items;
		for (const auto& [form, made] : snapshot.made) {
			std::string counts;
			for (std::size_t s = 0; s < made.size(); ++s) {
				totals[s] += made[s];
				if (made[s] > 0) {
					counts += std::format(",{}:{}", Quote(SourceVerb(static_cast<Source>(s))), made[s]);
				}
			}
			if (!items.empty()) {
				items.push_back(',');
			}
			items += std::format("{{\"id\":{},\"name\":{}{}}}", Quote(FormKey(form)), Quote(FormName(form)), counts);
		}
		body += "\"made\":{";
		for (std::size_t s = 0; s < totals.size(); ++s) {
			body += std::format("{}:{},", Quote(SourceVerb(static_cast<Source>(s))), totals[s]);
		}
		body += std::format("\"items\":[{}]}},", items);

		// Rules broken, in order.
		std::string broken;
		for (const auto& violation : snapshot.violations) {
			if (!broken.empty()) {
				broken.push_back(',');
			}
			broken += std::format("{{\"rule\":{},\"id\":{},\"name\":{},\"count\":{},\"day\":{:.2f}}}",
				Quote(RuleName(violation.rule)), Quote(FormKey(violation.form)), Quote(FormName(violation.form)), violation.count, violation.day);
		}
		body += std::format("\"rulesBroken\":[{}],", broken);

		// Kills by kind.
		std::string kills;
		for (const auto& [raceID, count] : snapshot.killsByRace) {
			if (!kills.empty()) {
				kills.push_back(',');
			}
			kills += std::format("{{\"kind\":{},\"id\":{},\"count\":{}}}", Quote(KindName(raceID)), Quote(FormKey(raceID)), count);
		}
		body += std::format("\"kills\":[{}],", kills);

		body += std::format("\"consoleOpened\":{},", snapshot.consoleOpened);
		// Followed by the mod itself, since the game keeps neither. Distance is in game units (about 70 to a metre).
		body += std::format("\"deaths\":{},\"distance\":{:.0f},", snapshot.deaths, snapshot.distance);

		// The game's own totals for the character, as last answered (up to a minute old).
		std::string stats;
		for (const auto& [key, value] : Stats::Latest()) {
			if (!stats.empty()) {
				stats += ",";
			}
			stats += std::format("{}:{}", Quote(key), value);
		}
		body += std::format("\"stats\":{{{}}},", stats);

		std::uint32_t pluginCount = 0;
		std::string   pluginHash;
		DescribePlugins(pluginCount, pluginHash);
		body += std::format("\"plugins\":{{\"count\":{},\"fingerprint\":{}}}", pluginCount, Quote(pluginHash));
		body += "}";
		return body;
	}
}

void RunFile::Write()
{
	if (!Events::OnChallenge()) {
		return;  // a standard playthrough has no run to report
	}
	const auto player = RE::PlayerCharacter::GetSingleton();
	auto       directory = SKSE::log::log_directory();
	if (!player || !directory) {
		logger::warn("run file not written: no player or no log directory");
		return;
	}

	try {
		const auto body = BuildBody();
		const auto seal = Sha256::Hex(Sha256::Hmac(kSealKey, body));
		const auto text = std::format("{{\"run\":{},\"seal\":\"{}\"}}\n", body, seal);

		*directory /= "ZenitharsChallenge";
		std::filesystem::create_directories(*directory);
		const auto path = *directory / (SafeFileName(player->GetName()) + ".json");

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file) {
			logger::warn("run file not written: cannot open {}", path.string());
			return;
		}
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		logger::debug("run file written: {} ({} bytes)", path.string(), text.size());
	} catch (const std::exception& e) {
		logger::warn("run file not written: {}", e.what());
	}
}
