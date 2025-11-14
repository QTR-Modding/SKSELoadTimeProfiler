#include "ReferenceUI.h"
#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <format>

#include "ExtraDataStrings.h"

#ifdef GetObject
#undef GetObject
#endif

using namespace MCP::Reference;

// TripletID methods are defined in MCP.cpp; do not redefine here.

namespace MCP::Reference::ExtraData {
    void FormCount::to_imgui() const { ImGui::PushID(this); UI::ReadOnlyField("Item:", static_cast<std::string>(form), "##item"); ImGui::SameLine(); UI::ReadOnlyField("Count Delta:", std::to_string(count), "##count"); ImGui::PopID(); }
    void RefKeyword::to_imgui() const { ImGui::PushID(this); UI::ReadOnlyField("Ref:", static_cast<std::string>(ref), "##ref"); ImGui::SameLine(); UI::ReadOnlyField("Keyword:", static_cast<std::string>(keyword), "##kw"); ImGui::PopID(); }
    void FactionChanges::to_imgui() const { ImGui::PushID(this); UI::ReadOnlyField("Crime Faction:", static_cast<std::string>(crimeFaction), "##crimefac"); ImGui::SameLine(); UI::ReadOnlyField("Remove Crime Faction:", removeCrimeFaction?"True":"False", "##removecrimefac"); for (auto& [faction, rank] : changes){ ImGui::Separator(); UI::ReadOnlyField("Faction:", static_cast<std::string>(faction), "##faction"); ImGui::SameLine(); UI::ReadOnlyField("Rank:", std::to_string(rank), "##rank"); } ImGui::PopID(); }
    void Action::to_imgui() const { ImGui::PushID(this); UI::ReadOnlyField("Action Ref:", static_cast<std::string>(actionRef), "##actionref"); ImGui::SameLine(); UI::ReadOnlyField("Action Type:", std::to_string(actionType), "##actiontype"); ImGui::PopID(); }
    void Lock::to_imgui() const { ImGui::PushID(this); UI::ReadOnlyField("Base Level:", std::to_string(base_level), "##base"); ImGui::SameLine(); UI::ReadOnlyField("Flags:", std::to_string(flags), "##flags"); ImGui::SameLine(); UI::ReadOnlyField("Number of Tries:", std::to_string(n_tries), "##tries"); ImGui::SameLine(); UI::ReadOnlyField("Key:", static_cast<std::string>(key), "##key"); ImGui::PopID(); }
    void StartingPosition::to_imgui() const { ImGui::PushID(this); UI::ReadOnlyField("Location:", static_cast<std::string>(location), "##location"); UI::ReadOnlyField("Position:", std::format("X: {:.2f}, Y: {:.2f}, Z: {:.2f}", position_rotation.first.x, position_rotation.first.y, position_rotation.first.z), "##position"); UI::ReadOnlyField("Rotation:", std::format("X: {:.2f}, Y: {:.2f}, Z: {:.2f}", position_rotation.second.x, position_rotation.second.y, position_rotation.second.z), "##rotation"); ImGui::PopID(); }
    void Package::to_imgui() const { ImGui::PushID(this); UI::ReadOnlyField("Package:", static_cast<std::string>(package), "##package"); UI::ReadOnlyField("Index:", std::to_string(index), "##index"); UI::ReadOnlyField("Target:", static_cast<std::string>(target), "##target"); UI::ReadOnlyField("Action Complete:", actionComplete?"True":"False", "##actioncomplete"); ImGui::SameLine(); UI::ReadOnlyField("Activated:", activated?"True":"False", "##activated"); ImGui::SameLine(); UI::ReadOnlyField("Done Once:", doneOnce?"True":"False", "##doneonce"); ImGui::PopID(); }
}

namespace MCP::Reference {
    Info ref_info;

    void Info::FillExtraData() { if (!ref_) return; extradata.clear(); for (const auto &extra : ref_->extraList) extradata.emplace_back(extra.GetType()); std::apply([](auto&... h){ (h.data.clear(), ...); }, holders); for (const auto& extra : ref_->extraList){ const RE::BSExtraData* e=&extra; std::apply([&](auto&... h){ (h.ingest(e), ...); }, holders); } }
    Info::Info(RE::TESObjectREFR* a_refr){ if(a_refr){ ref_=a_refr->GetHandle().get(); ref=TripletID(a_refr); base=TripletID(a_refr->GetBaseObject()); if(auto loc= ref_->GetCurrentLocation()) location=TripletID(loc); if(auto fac=a_refr->GetFactionOwner()) faction=TripletID(fac); FillExtraData(); } }
    void UpdateRefInfo(RE::TESObjectREFR* a_refr){ ref_info = Info(a_refr); }

    template <std::size_t I=0, typename F> static bool visit_by_type(const Info::Holders& holders, RE::ExtraDataType t, F&& f){ if constexpr(I>= std::tuple_size_v<Info::Holders>) return false; else { using H = std::tuple_element_t<I, Info::Holders>; if (H::GetType()==t){ std::forward<F>(f)(std::get<I>(holders)); return true; } return visit_by_type<I+1>(holders,t,std::forward<F>(f)); } }
    void PrintExtraData(){ if(!ref_info){ ImGui::Text("No reference selected."); return; } if(ImGui::CollapsingHeader("ExtraData")){ ImGui::Indent(); for(auto entry: ref_info.extradata){ if(const char* header=ExtraDataTypeName(entry); !ImGui::CollapsingHeader(header)) continue; bool shown = visit_by_type(ref_info.holders, entry, [](const auto& holder){ for(const auto& x: holder.data) x.to_imgui(); }); if(!shown) ImGui::TextUnformatted("No viewer implemented for this type."); } ImGui::Unindent(); } }
    void PrintRefInfo(){ if(!ref_info){ ImGui::Text("No reference selected."); return; } ImGui::Text("Reference:"); ref_info.ref.to_imgui(); ImGui::Text("Base Object:"); ref_info.base.to_imgui(); ImGui::Text("Location:"); ref_info.location.to_imgui(); ImGui::Text("Faction:"); ref_info.faction.to_imgui(); ImGui::Separator(); PrintExtraData(); }
    void __stdcall Render(){ static char searchBuffer[64]=""; bool submit = ImGui::InputTextWithHint("Search", "Enter FormID or EditorID", searchBuffer, sizeof(searchBuffer), ImGuiInputTextFlags_EnterReturnsTrue); ImGui::SameLine(); if(ImGui::Button("Find Reference")||submit){ std::string reference = searchBuffer; RE::TESObjectREFR* form = nullptr; if (const auto hex = Utilities::hex_to_u32(reference)) form = RE::TESForm::LookupByID<RE::TESObjectREFR>(hex.value()); UpdateRefInfo(form); } if(ImGui::Button("Get Console Reference")){ if(auto sel = RE::Console::GetSelectedRef()) UpdateRefInfo(sel.get()); } PrintRefInfo(); }
}

// Updated filldata implementations
namespace MCP::Reference::ExtraData {
    void filldata(const RE::ExtraContainerChanges* a_extradata, std::vector<FormCount>& a_data){ if(!a_extradata||!a_extradata->changes||!a_extradata->changes->entryList) return; for(auto& entry : *a_extradata->changes->entryList){ if(entry) a_data.emplace_back(entry->object, entry->countDelta); } }
    void filldata(const RE::ExtraLinkedRef* a_extradata, std::vector<RefKeyword>& a_data){ if(!a_extradata) return; for(const auto& [kw,refr]: a_extradata->linkedRefs) a_data.emplace_back(refr, kw); }
    void filldata(const RE::ExtraLinkedRefChildren* a_extradata, std::vector<RefKeyword>& a_data){ if(!a_extradata) return; for(const auto& e: a_extradata->linkedChildren) a_data.emplace_back(e.refr.get().get(), e.keyword); }
    void filldata(const RE::ExtraPromotedRef* a_extradata, std::vector<TripletID>& a_data){ if(!a_extradata) return; for(const auto& owner: a_extradata->promotedRefOwners) a_data.emplace_back(owner); }
    void filldata(const RE::ExtraFactionChanges* a_extradata, std::vector<FactionChanges>& a_data){ if(!a_extradata) return; FactionChanges fc; for(const auto& ch: a_extradata->factionChanges) fc.changes.emplace_back(TripletID(ch.faction), ch.rank); fc.crimeFaction=TripletID(a_extradata->crimeFaction); fc.removeCrimeFaction=a_extradata->removeCrimeFaction; a_data.emplace_back(std::move(fc)); }
    void filldata(const RE::ExtraOwnership* a_extradata, std::vector<TripletID>& a_data){ if(!a_extradata) return; a_data.emplace_back(a_extradata->owner); }
    void filldata(const RE::ExtraAction* a_extradata, std::vector<Action>& a_data){ if(!a_extradata) return; if(a_extradata->actionRef) a_data.emplace_back(a_extradata->actionRef, a_extradata->action.underlying()); }
    void filldata(const RE::ExtraLocationRefType* a_extradata, std::vector<TripletID>& a_data){ if(!a_extradata) return; if(a_extradata->locRefType) a_data.emplace_back(a_extradata->locRefType); }
    void filldata(const RE::ExtraLocation* a_extradata, std::vector<TripletID>& a_data){ if(!a_extradata) return; if(a_extradata->location) a_data.emplace_back(a_extradata->location); }
    void filldata(const RE::ExtraPackage* a_extradata, std::vector<Package>& a_data){ if(!a_extradata) return; Package pkg; pkg.package=TripletID(a_extradata->unk10); pkg.index=a_extradata->index; pkg.target=TripletID(a_extradata->target.get().get()); pkg.actionComplete=a_extradata->actionComplete; pkg.activated=a_extradata->activated; pkg.doneOnce=a_extradata->doneOnce; a_data.emplace_back(std::move(pkg)); }
    void filldata(const RE::ExtraLock* a_extradata, std::vector<Lock>& a_data){ if(!a_extradata||!a_extradata->lock) return; Lock lockData; lockData.base_level=a_extradata->lock->baseLevel; lockData.flags=a_extradata->lock->flags.underlying(); lockData.n_tries=a_extradata->lock->numTries; lockData.key=TripletID(a_extradata->lock->key); a_data.emplace_back(std::move(lockData)); }
    void filldata(const RE::ExtraStartingPosition* a_extradata, std::vector<StartingPosition>& a_data){ if(!a_extradata) return; StartingPosition sp; sp.location=TripletID(a_extradata->location); sp.position_rotation={ a_extradata->startPosition.pos, a_extradata->startPosition.rot }; a_data.emplace_back(std::move(sp)); }
}
