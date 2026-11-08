#include "HiddenStore.h"

namespace
{
	constexpr std::uint32_t kUniqueID = 'JDCL';
	constexpr std::uint32_t kRecordQuests = 'HIDQ';
	constexpr std::uint32_t kRecordMisc = 'HIDM';
	constexpr std::uint32_t kRecordVersion = 1;
}

HiddenStore& HiddenStore::Get()
{
	static HiddenStore instance;
	return instance;
}

bool HiddenStore::IsHidden(RE::FormID a_formID, std::uint32_t a_instance) const
{
	return _quests.contains(MakeKey(a_formID, a_instance));
}

bool HiddenStore::IsMiscHidden(RE::FormID a_formID, std::uint32_t a_instance) const
{
	return _misc.contains(MakeKey(a_formID, a_instance));
}

void HiddenStore::Hide(RE::FormID a_formID, std::uint32_t a_instance)
{
	_quests.insert(MakeKey(a_formID, a_instance));
	logger::debug("hide quest {:08X}/{} (now {} hidden)", a_formID, a_instance, _quests.size());
}

void HiddenStore::Unhide(RE::FormID a_formID, std::uint32_t a_instance)
{
	_quests.erase(MakeKey(a_formID, a_instance));
	logger::debug("unhide quest {:08X}/{} (now {} hidden)", a_formID, a_instance, _quests.size());
}

void HiddenStore::HideMisc(RE::FormID a_formID, std::uint32_t a_instance)
{
	_misc.insert(MakeKey(a_formID, a_instance));
	logger::debug("hide misc {:08X}/{} (now {} hidden)", a_formID, a_instance, _misc.size());
}

void HiddenStore::UnhideMisc(RE::FormID a_formID, std::uint32_t a_instance)
{
	_misc.erase(MakeKey(a_formID, a_instance));
	logger::debug("unhide misc {:08X}/{} (now {} hidden)", a_formID, a_instance, _misc.size());
}

void HiddenStore::Clear()
{
	_quests.clear();
	_misc.clear();
}

void HiddenStore::RegisterSerialization()
{
	const auto intfc = SKSE::GetSerializationInterface();
	intfc->SetUniqueID(kUniqueID);
	intfc->SetSaveCallback(OnSave);
	intfc->SetLoadCallback(OnLoad);
	intfc->SetRevertCallback(OnRevert);
}

void HiddenStore::WriteSet(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, const std::unordered_set<Key>& a_set)
{
	if (!a_intfc->OpenRecord(a_type, kRecordVersion)) {
		logger::error("failed to open record {:08X}", a_type);
		return;
	}
	const auto count = static_cast<std::uint32_t>(a_set.size());
	a_intfc->WriteRecordData(count);
	for (const auto key : a_set) {
		const auto formID = static_cast<std::uint32_t>(key >> 32);
		const auto instance = static_cast<std::uint32_t>(key & 0xFFFFFFFFu);
		a_intfc->WriteRecordData(formID);
		a_intfc->WriteRecordData(instance);
	}
}

void HiddenStore::ReadSet(SKSE::SerializationInterface* a_intfc, std::unordered_set<Key>& a_set)
{
	std::uint32_t count = 0;
	if (!a_intfc->ReadRecordData(count)) {
		return;
	}
	for (std::uint32_t i = 0; i < count; ++i) {
		std::uint32_t oldID = 0;
		std::uint32_t instance = 0;
		if (!a_intfc->ReadRecordData(oldID) || !a_intfc->ReadRecordData(instance)) {
			logger::warn("truncated hidden-quest record after {} of {} entries", i, count);
			return;
		}
		RE::FormID newID = 0;
		// Maps the saved FormID onto the current load order; fails if the plugin is gone.
		if (!a_intfc->ResolveFormID(oldID, newID) || newID == 0) {
			logger::debug("dropping hidden entry {:08X}/{}: plugin no longer loaded", oldID, instance);
			continue;
		}
		a_set.insert(MakeKey(newID, instance));
	}
}

void HiddenStore::OnSave(SKSE::SerializationInterface* a_intfc)
{
	auto& self = Get();
	WriteSet(a_intfc, kRecordQuests, self._quests);
	WriteSet(a_intfc, kRecordMisc, self._misc);
	logger::debug("saved {} hidden quests, {} hidden misc objectives", self._quests.size(), self._misc.size());
}

void HiddenStore::OnLoad(SKSE::SerializationInterface* a_intfc)
{
	auto& self = Get();
	self.Clear();
	self.showHidden = false;

	std::uint32_t type = 0;
	std::uint32_t version = 0;
	std::uint32_t length = 0;
	while (a_intfc->GetNextRecordInfo(type, version, length)) {
		if (version != kRecordVersion) {
			logger::warn("unknown record version {} for {:08X}; skipping", version, type);
			continue;
		}
		switch (type) {
		case kRecordQuests:
			ReadSet(a_intfc, self._quests);
			break;
		case kRecordMisc:
			ReadSet(a_intfc, self._misc);
			break;
		default:
			logger::warn("unknown record type {:08X}; skipping", type);
			break;
		}
	}
	logger::info("loaded {} hidden quests, {} hidden misc objectives", self._quests.size(), self._misc.size());
}

void HiddenStore::OnRevert(SKSE::SerializationInterface*)
{
	auto& self = Get();
	self.Clear();
	self.showHidden = false;
}
