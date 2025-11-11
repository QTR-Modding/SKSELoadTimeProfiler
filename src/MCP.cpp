#include "MCP.h"
#include "CLibUtilsQTR/FormReader.hpp"
#include "SKSEMCP/SKSEMenuFramework.hpp"

#undef GetObject

void MCP::Register()
{

    if (!SKSEMenuFramework::IsInstalled()) {
		logger::error("SKSEMenuFramework is not installed.");
		return;
	}

    SKSEMenuFramework::SetSection(Utilities::mod_name);
    SKSEMenuFramework::AddSectionItem("Reference", Reference::Render);
    SKSEMenuFramework::AddSectionItem("Log", RenderLog);
}

void __stdcall MCP::RenderLog()
{
	// add checkboxes to filter log levels
    ImGui::Checkbox("Trace", &LogSettings::log_trace);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &LogSettings::log_info);
    ImGui::SameLine();
    ImGui::Checkbox("Warning", &LogSettings::log_warning);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &LogSettings::log_error);


    // if "Generate Log" button is pressed, read the log file
    if (ImGui::Button("Generate Log")) {
		logLines = Utilities::ReadLogFile();
	}

    // Display each line in a new ImGui::Text() element
    for (const auto& line : logLines) {
        if (line.find("trace") != std::string::npos && !LogSettings::log_trace) continue;
        if (line.find("info") != std::string::npos && !LogSettings::log_info) continue;
        if (line.find("warning") != std::string::npos && !LogSettings::log_warning) continue;
        if (line.find("error") != std::string::npos && !LogSettings::log_error) continue;
		ImGui::Text(line.c_str());
	}
}

void __stdcall MCP::Reference::Render()
{
	//text input for formID or editorID of the reference to search for
	static char searchBuffer[64] = "";
    if (ImGui::InputTextWithHint("Search", "Enter FormID or EditorID", searchBuffer, sizeof(searchBuffer))) {
    }
	ImGui::SameLine();
    if (ImGui::Button("Find Reference")) {
	    reference = std::string(searchBuffer);
        if (const auto formID = FormReader::GetFormEditorIDFromString(reference)) {
            if (const auto form = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID)) {
				UpdateRefInfo(form);
            }
        }
	}
	ImGui::SameLine();
    if (ImGui::Button("Get Console Reference")) {
        if (const auto a_ref = RE::Console::GetSelectedRef()) {
			UpdateRefInfo(a_ref.get());
        }
    }

    PrintRefInfo();

    PrintExtraData();

}

void MCP::Reference::UpdateRefInfo(RE::TESObjectREFR* a_refr)
{
    
    if (a_refr) {
        ref_info.ref = a_refr->GetHandle().get();
        ref_info.name = ref_info.ref->GetName();
		ref_info.refid = ref_info.ref->GetFormID();
        ref_info.location = ref_info.ref->GetCurrentLocation();
        if (ref_info.location) {
			ref_info.location_name = ref_info.location->GetFullName();
        }
		ref_info.faction = a_refr->GetFactionOwner();
		if (ref_info.faction) {
			ref_info.faction_name = ref_info.faction->GetName();
            if (ref_info.faction->IsVendor()) {
				auto& vendor_data = ref_info.faction->vendorData;
                ref_info.merchantChest = vendor_data.merchantContainer;
				if (ref_info.merchantChest) {
					ref_info.merchantChest_name = ref_info.merchantChest->GetName();
                }
            }
        }
        ref_info.extradata.clear();
        for (const auto& entry : ref_info.ref->extraList) {
			ref_info.extradata.push_back(entry.GetType());
        }
    } else {
		ref_info.ref = nullptr;
		ref_info.refid = 0;
        ref_info.name = "";
        ref_info.location = nullptr;
		ref_info.location_name = "";
		ref_info.faction = nullptr;
        ref_info.faction_name = "";
		ref_info.extradata.clear();
	}

    if (const auto changes = ref_info.ref->extraList.GetByType<RE::ExtraContainerChanges>()) {
		ref_info.inventory_changes.clear();
        for (auto it = changes->changes->entryList->begin(); it != changes->changes->entryList->end(); ++it) {
            if (const auto& item_entry = *it) {
                const auto a_form_name = item_entry->GetDisplayName();
				const auto a_count_delta = item_entry->countDelta;
				ref_info.inventory_changes.push_back({ a_form_name, a_count_delta });
            }
        }
    }

    if (const auto linkedRef = ref_info.ref->extraList.GetByType<RE::ExtraLinkedRef>()) {
        ref_info.linked_refs.clear();
        for (const auto& entry : linkedRef->linkedRefs) {
			auto ref_name = entry.refr ? entry.refr->GetName() : "None";
			auto ref_id = entry.refr ? entry.refr->GetFormID() : 0;
			auto kw = entry.keyword ? clib_util::editorID::get_editorID(entry.keyword) : "None";
            ref_info.linked_refs.push_back({ ref_name, ref_id, kw });
        }
    }

    if (const auto promotedRef = ref_info.ref->extraList.GetByType<RE::ExtraPromotedRef>()) {
		for (const auto& owner : promotedRef->promotedRefOwners) {
            auto ref_name = owner ? owner->GetName() : "None";
            auto ref_id = owner ? owner->GetFormID() : 0;
			ref_info.promoted_refs.push_back({ ref_name, ref_id });
		}
    }
}

void MCP::Reference::PrintRefInfo()
{
    ImGui::Text("Reference Info:");
    if (ref_info.ref) {
        ImGui::Text("Name: %s", ref_info.name.c_str());
        ImGui::Text("FormID: %08X", ref_info.ref->GetFormID());
        if (ref_info.location) {
            ImGui::Text("Location: %s", ref_info.location->GetFullName());
            ImGui::Text("Location FormID: %08X", ref_info.location->GetFormID());
        }
        if (ref_info.faction) {
            ImGui::Text("Faction: %s", ref_info.faction_name.c_str());
        }
        if (ref_info.merchantChest) {
            ImGui::Text("Merchant Chest: %s", ref_info.merchantChest_name.c_str());
            ImGui::Text("Merchant Chest FormID: %08X", ref_info.merchantChest->GetFormID());
        }
    } else {
        ImGui::Text("No reference selected.");
	}
}

void MCP::Reference::PrintExtraData()
{
    for (const auto& entry : ref_info.extradata) {
        const char* name = "Unknown";
        switch (entry) {
        case RE::ExtraDataType::kNone: name = "None"; break;
        case RE::ExtraDataType::kHavok: name = "ExtraHavok"; break;
        case RE::ExtraDataType::kCell3D: name = "ExtraCell3D"; break;
        case RE::ExtraDataType::kCellWaterType: name = "ExtraCellWaterType"; break;
        case RE::ExtraDataType::kRegionList: name = "ExtraRegionList"; break;
        case RE::ExtraDataType::kSeenData: name = "ExtraSeenData"; break;
        case RE::ExtraDataType::kEditorID: name = "ExtraEditorID"; break;
        case RE::ExtraDataType::kCellMusicType: name = "ExtraCellMusicType"; break;
        case RE::ExtraDataType::kCellSkyRegion: name = "ExtraCellSkyRegion"; break;
        case RE::ExtraDataType::kProcessMiddleLow: name = "ExtraProcessMiddleLow"; break;
        case RE::ExtraDataType::kDetachTime: name = "ExtraDetachTime"; break;
        case RE::ExtraDataType::kPersistentCell: name = "ExtraPersistentCell"; break;
        case RE::ExtraDataType::kUnk0C: name = "Unk0C"; break;
        case RE::ExtraDataType::kAction: name = "ExtraAction"; break;
        case RE::ExtraDataType::kStartingPosition: name = "ExtraStartingPosition"; break;
        case RE::ExtraDataType::kUnk0F: name = "Unk0F"; break;
        case RE::ExtraDataType::kAnimGraphManager: name = "ExtraAnimGraphManager"; break;
        case RE::ExtraDataType::kBiped: name = "ExtraBiped"; break;
        case RE::ExtraDataType::kUsedMarkers: name = "ExtraUsedMarkers"; break;
        case RE::ExtraDataType::kDistantData: name = "ExtraDistantData"; break;
        case RE::ExtraDataType::kRagDollData: name = "ExtraRagDollData"; break;
        case RE::ExtraDataType::kContainerChanges: name = "ExtraContainerChanges"; break;
        case RE::ExtraDataType::kWorn: name = "ExtraWorn"; break;
        case RE::ExtraDataType::kWornLeft: name = "ExtraWornLeft"; break;
        case RE::ExtraDataType::kPackageStartLocation: name = "ExtraPackageStartLocation"; break;
        case RE::ExtraDataType::kPackage: name = "ExtraPackage"; break;
        case RE::ExtraDataType::kTresPassPackage: name = "ExtraTresPassPackage"; break;
        case RE::ExtraDataType::kRunOncePacks: name = "ExtraRunOncePacks"; break;
        case RE::ExtraDataType::kReferenceHandle: name = "ExtraReferenceHandle"; break;
        case RE::ExtraDataType::kFollower: name = "ExtraFollower"; break;
        case RE::ExtraDataType::kLevCreaModifier: name = "ExtraLevCreaModifier"; break;
        case RE::ExtraDataType::kGhost: name = "ExtraGhost"; break;
        case RE::ExtraDataType::kOriginalReference: name = "ExtraOriginalReference"; break;
        case RE::ExtraDataType::kOwnership: name = "ExtraOwnership"; break;
        case RE::ExtraDataType::kGlobal: name = "ExtraGlobal"; break;
        case RE::ExtraDataType::kRank: name = "ExtraRank"; break;
        case RE::ExtraDataType::kCount: name = "ExtraCount"; break;
        case RE::ExtraDataType::kHealth: name = "ExtraHealth"; break;
        case RE::ExtraDataType::kUnk26: name = "Unk26"; break;
        case RE::ExtraDataType::kTimeLeft: name = "ExtraTimeLeft"; break;
        case RE::ExtraDataType::kCharge: name = "ExtraCharge"; break;
        case RE::ExtraDataType::kLight: name = "ExtraLight"; break;
        case RE::ExtraDataType::kLock: name = "ExtraLock"; break;
        case RE::ExtraDataType::kTeleport: name = "ExtraTeleport"; break;
        case RE::ExtraDataType::kMapMarker: name = "ExtraMapMarker"; break;
        case RE::ExtraDataType::kLeveledCreature: name = "ExtraLeveledCreature"; break;
        case RE::ExtraDataType::kLeveledItem: name = "ExtraLeveledItem"; break;
        case RE::ExtraDataType::kScale: name = "ExtraScale"; break;
        case RE::ExtraDataType::kMissingLinkedRefIDs: name = "ExtraMissingLinkedRefIDs"; break;
        case RE::ExtraDataType::kMagicCaster: name = "ExtraMagicCaster"; break;
        case RE::ExtraDataType::kNonActorMagicTarget: name = "NonActorMagicTarget"; break;
        case RE::ExtraDataType::kUnk33: name = "Unk33"; break;
        case RE::ExtraDataType::kPlayerCrimeList: name = "ExtraPlayerCrimeList"; break;
        case RE::ExtraDataType::kUnk35: name = "Unk35"; break;
        case RE::ExtraDataType::kEnableStateParent: name = "ExtraEnableStateParent"; break;
        case RE::ExtraDataType::kEnableStateChildren: name = "ExtraEnableStateChildren"; break;
        case RE::ExtraDataType::kItemDropper: name = "ExtraItemDropper"; break;
        case RE::ExtraDataType::kDroppedItemList: name = "ExtraDroppedItemList"; break;
        case RE::ExtraDataType::kRandomTeleportMarker: name = "ExtraRandomTeleportMarker"; break;
        case RE::ExtraDataType::kUnk3B: name = "Unk3B"; break;
        case RE::ExtraDataType::kSavedHavokData: name = "ExtraSavedHavokData"; break;
        case RE::ExtraDataType::kCannotWear: name = "ExtraCannotWear"; break;
        case RE::ExtraDataType::kPoison: name = "ExtraPoison"; break;
        case RE::ExtraDataType::kMagicLight: name = "ExtraMagicLight"; break;
        case RE::ExtraDataType::kLastFinishedSequence: name = "ExtraLastFinishedSequence"; break;
        case RE::ExtraDataType::kSavedAnimation: name = "ExtraSavedAnimation"; break;
        case RE::ExtraDataType::kNorthRotation: name = "ExtraNorthRotation"; break;
        case RE::ExtraDataType::kSpawnContainer: name = "ExtraSpawnContainer"; break;
        case RE::ExtraDataType::kFriendHits: name = "ExtraFriendHits"; break;
        case RE::ExtraDataType::kHeadingTarget: name = "ExtraHeadingTarget"; break;
        case RE::ExtraDataType::kUnk46: name = "Unk46"; break;
        case RE::ExtraDataType::kRefractionProperty: name = "ExtraRefractionProperty"; break;
        case RE::ExtraDataType::kStartingWorldOrCell: name = "ExtraStartingWorldOrCell"; break;
        case RE::ExtraDataType::kHotkey: name = "ExtraHotkey"; break;
        case RE::ExtraDataType::kEditorRef3DData: name = "ExtraEditorRef3DData"; break;
        case RE::ExtraDataType::kEditorRefMoveData: name = "ExtraEditorRefMoveData"; break;
        case RE::ExtraDataType::kInfoGeneralTopic: name = "ExtraInfoGeneralTopic"; break;
        case RE::ExtraDataType::kHasNoRumors: name = "ExtraHasNoRumors"; break;
        case RE::ExtraDataType::kSound: name = "ExtraSound"; break;
        case RE::ExtraDataType::kTerminalState: name = "ExtraTerminalState"; break;
        case RE::ExtraDataType::kLinkedRef: name = "ExtraLinkedRef"; break;
        case RE::ExtraDataType::kLinkedRefChildren: name = "ExtraLinkedRefChildren"; break;
        case RE::ExtraDataType::kActivateRef: name = "ExtraActivateRef"; break;
        case RE::ExtraDataType::kActivateRefChildren: name = "ExtraActivateRefChildren"; break;
        case RE::ExtraDataType::kCanTalkToPlayer: name = "ExtraCanTalkToPlayer"; break;
        case RE::ExtraDataType::kObjectHealth: name = "ExtraObjectHealth"; break;
        case RE::ExtraDataType::kCellImageSpace: name = "ExtraCellImageSpace"; break;
        case RE::ExtraDataType::kNavMeshPortal: name = "ExtraNavMeshPortal"; break;
        case RE::ExtraDataType::kModelSwap: name = "ExtraModelSwap"; break;
        case RE::ExtraDataType::kRadius: name = "ExtraRadius"; break;
        case RE::ExtraDataType::kUnk5A: name = "Unk5A"; break;
        case RE::ExtraDataType::kFactionChanges: name = "ExtraFactionChanges"; break;
        case RE::ExtraDataType::kDismemberedLimbs: name = "ExtraDismemberedLimbs"; break;
        case RE::ExtraDataType::kActorCause: name = "ExtraActorCause"; break;
        case RE::ExtraDataType::kMultiBound: name = "ExtraMultiBound"; break;
        case RE::ExtraDataType::kMultiBoundMarkerData: name = "MultiBoundMarkerData"; break;
        case RE::ExtraDataType::kMultiBoundRef: name = "ExtraMultiBoundRef"; break;
        case RE::ExtraDataType::kReflectedRefs: name = "ExtraReflectedRefs"; break;
        case RE::ExtraDataType::kReflectorRefs: name = "ExtraReflectorRefs"; break;
        case RE::ExtraDataType::kEmittanceSource: name = "ExtraEmittanceSource"; break;
        case RE::ExtraDataType::kUnk64: name = "Unk64"; break;
        case RE::ExtraDataType::kCombatStyle: name = "ExtraCombatStyle"; break;
        case RE::ExtraDataType::kUnk66: name = "Unk66"; break;
        case RE::ExtraDataType::kPrimitive: name = "ExtraPrimitive"; break;
        case RE::ExtraDataType::kOpenCloseActivateRef: name = "ExtraOpenCloseActivateRef"; break;
        case RE::ExtraDataType::kAnimNoteReceiver: name = "ExtraAnimNoteReceiver"; break;
        case RE::ExtraDataType::kAmmo: name = "ExtraAmmo"; break;
        case RE::ExtraDataType::kPatrolRefData: name = "ExtraPatrolRefData"; break;
        case RE::ExtraDataType::kPackageData: name = "ExtraPackageData"; break;
        case RE::ExtraDataType::kOcclusionShape: name = "ExtraOcclusionShape"; break;
        case RE::ExtraDataType::kCollisionData: name = "ExtraCollisionData"; break;
        case RE::ExtraDataType::kSayTopicInfoOnceADay: name = "ExtraSayTopicInfoOnceADay"; break;
        case RE::ExtraDataType::kEncounterZone: name = "ExtraEncounterZone"; break;
        case RE::ExtraDataType::kSayTopicInfo: name = "ExtraSayToTopicInfo"; break;
        case RE::ExtraDataType::kOcclusionPlaneRefData: name = "ExtraOcclusionPlaneRefData"; break;
        case RE::ExtraDataType::kPortalRefData: name = "ExtraPortalRefData"; break;
        case RE::ExtraDataType::kPortal: name = "ExtraPortal"; break;
        case RE::ExtraDataType::kRoom: name = "ExtraRoom"; break;
        case RE::ExtraDataType::kHealthPerc: name = "ExtraHealthPerc"; break;
        case RE::ExtraDataType::kRoomRefData: name = "ExtraRoomRefData"; break;
        case RE::ExtraDataType::kGuardedRefData: name = "ExtraGuardedRefData"; break;
        case RE::ExtraDataType::kCreatureAwakeSound: name = "ExtraCreatureAwakeSound"; break;
        case RE::ExtraDataType::kUnk7A: name = "Unk7A"; break;
        case RE::ExtraDataType::kHorse: name = "ExtraHorse"; break;
        case RE::ExtraDataType::kIgnoredBySandbox: name = "ExtraIgnoredBySandbox"; break;
        case RE::ExtraDataType::kCellAcousticSpace: name = "ExtraCellAcousticSpace"; break;
        case RE::ExtraDataType::kReservedMarkers: name = "ExtraReservedMarkers"; break;
        case RE::ExtraDataType::kWeaponIdleSound: name = "ExtraWeaponIdleSound"; break;
        case RE::ExtraDataType::kWaterLightRefs: name = "ExtraWaterLightRefs"; break;
        case RE::ExtraDataType::kLitWaterRefs: name = "ExtraLitWaterRefs"; break;
        case RE::ExtraDataType::kWeaponAttackSound: name = "ExtraWeaponAttackSound"; break;
        case RE::ExtraDataType::kActivateLoopSound: name = "ExtraActivateLoopSound"; break;
        case RE::ExtraDataType::kPatrolRefInUseData: name = "ExtraPatrolRefInUseData"; break;
        case RE::ExtraDataType::kAshPileRef: name = "ExtraAshPileRef"; break;
        case RE::ExtraDataType::kCreatureMovementSound: name = "ExtraCreatureMovementSound"; break;
        case RE::ExtraDataType::kFollowerSwimBreadcrumbs: name = "ExtraFollowerSwimBreadcrumbs"; break;
        case RE::ExtraDataType::kAliasInstanceArray: name = "ExtraAliasInstanceArray"; break;
        case RE::ExtraDataType::kLocation: name = "ExtraLocation"; break;
        case RE::ExtraDataType::kUnk8A: name = "Unk8A"; break;
        case RE::ExtraDataType::kLocationRefType: name = "ExtraLocationRefType"; break;
        case RE::ExtraDataType::kPromotedRef: name = "ExtraPromotedRef"; break;
        case RE::ExtraDataType::kAnimationSequencer: name = "ExtraAnimationSequencer"; break;
        case RE::ExtraDataType::kOutfitItem: name = "ExtraOutfitItem"; break;
        case RE::ExtraDataType::kUnk8F: name = "Unk8F"; break;
        case RE::ExtraDataType::kLeveledItemBase: name = "ExtraLeveledItemBase"; break;
        case RE::ExtraDataType::kLightData: name = "ExtraLightData"; break;
        case RE::ExtraDataType::kSceneData: name = "ExtraSceneData"; break;
        case RE::ExtraDataType::kBadPosition: name = "ExtraBadPosition"; break;
        case RE::ExtraDataType::kHeadTrackingWeight: name = "ExtraHeadTrackingWeight"; break;
        case RE::ExtraDataType::kFromAlias: name = "ExtraFromAlias"; break;
        case RE::ExtraDataType::kShouldWear: name = "ExtraShouldWear"; break;
        case RE::ExtraDataType::kFavorCost: name = "ExtraFavorCost"; break;
        case RE::ExtraDataType::kAttachedArrows3D: name = "ExtraAttachedArrows3D"; break;
        case RE::ExtraDataType::kTextDisplayData: name = "ExtraTextDisplayData"; break;
        case RE::ExtraDataType::kAlphaCutoff: name = "ExtraAlphaCutoff"; break;
        case RE::ExtraDataType::kEnchantment: name = "ExtraEnchantment"; break;
        case RE::ExtraDataType::kSoul: name = "ExtraSoul"; break;
        case RE::ExtraDataType::kForcedTarget: name = "ExtraForcedTarget"; break;
        case RE::ExtraDataType::kUnk9E: name = "Unk9E"; break;
        case RE::ExtraDataType::kUniqueID: name = "ExtraUniqueID"; break;
        case RE::ExtraDataType::kFlags: name = "ExtraFlags"; break;
        case RE::ExtraDataType::kRefrPath: name = "ExtraRefrPath"; break;
        case RE::ExtraDataType::kDecalGroup: name = "ExtraDecalGroup"; break;
        case RE::ExtraDataType::kLockList: name = "ExtraLockList"; break;
        case RE::ExtraDataType::kForcedLandingMarker: name = "ExtraForcedLandingMarker"; break;
        case RE::ExtraDataType::kLargeRefOwnerCells: name = "ExtraLargeRefOwnerCells"; break;
        case RE::ExtraDataType::kCellWaterEnvMap: name = "ExtraCellWaterEnvMap"; break;
        case RE::ExtraDataType::kCellGrassData: name = "ExtraCellGrassData"; break;
        case RE::ExtraDataType::kTeleportName: name = "ExtraTeleportName"; break;
        case RE::ExtraDataType::kInteraction: name = "ExtraInteraction"; break;
        case RE::ExtraDataType::kWaterData: name = "ExtraWaterData"; break;
        case RE::ExtraDataType::kWaterCurrentZoneData: name = "ExtraWaterCurrentZoneData"; break;
        case RE::ExtraDataType::kAttachRef: name = "ExtraAttachRef"; break;
        case RE::ExtraDataType::kAttachRefChildren: name = "ExtraAttachRefChildren"; break;
        case RE::ExtraDataType::kGroupConstraint: name = "ExtraGroupConstraint"; break;
        case RE::ExtraDataType::kScriptedAnimDependence: name = "ExtraScriptedAnimDependence"; break;
        case RE::ExtraDataType::kCachedScale: name = "ExtraCachedScale"; break;
        case RE::ExtraDataType::kRaceData: name = "ExtraRaceData"; break;
        case RE::ExtraDataType::kGIDBuffer: name = "ExtraGIDBuffer"; break;
        case RE::ExtraDataType::kMissingRefIDs: name = "ExtraMissingRefIDs"; break;
        case RE::ExtraDataType::kUnkB4: name = "UnkB4"; break;
        case RE::ExtraDataType::kResourcesPreload: name = "ExtraResourcesPreload"; break;
        case RE::ExtraDataType::kUnkB6: name = "UnkB6"; break;
        case RE::ExtraDataType::kUnkB7: name = "UnkB7"; break;
        case RE::ExtraDataType::kUnkB8: name = "UnkB8"; break;
        case RE::ExtraDataType::kUnkB9: name = "UnkB9"; break;
        case RE::ExtraDataType::kUnkBA: name = "UnkBA"; break;
        case RE::ExtraDataType::kUnkBB: name = "UnkBB"; break;
        case RE::ExtraDataType::kUnkBC: name = "UnkBC"; break;
        case RE::ExtraDataType::kUnkBD: name = "UnkBD"; break;
        case RE::ExtraDataType::kUnkBE: name = "UnkBE"; break;
        case RE::ExtraDataType::kUnkBF: name = "UnkBF"; break;
        default: break;
        }

        if (ImGui::CollapsingHeader(name)) {
            if (entry == RE::ExtraDataType::kContainerChanges) {
                for (const auto& item : ref_info.inventory_changes) {
                    ImGui::Text(std::format("Item: {}, Count Delta: {}", item.form_name, item.count).c_str());
                }
            }
            if (entry == RE::ExtraDataType::kLinkedRef) {
                for (const auto& linked_ref : ref_info.linked_refs) {
                    ImGui::Text(std::format("Ref Name: {}, FormID: {:08X}, Keyword: {}", linked_ref.ref_name, linked_ref.refid, linked_ref.keyword).c_str());
				}
            }
            if (entry == RE::ExtraDataType::kPromotedRef) {
                for (const auto& promoted_ref : ref_info.promoted_refs) {
					ImGui::Text(std::format("Ref Name: {}, FormID: {:08X}", promoted_ref.ref_name, promoted_ref.refid).c_str());
                }
			}
        }

    }
}
