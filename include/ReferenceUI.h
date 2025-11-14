#pragma once
#include "MCP.h"
#include "SKSE/Interfaces.h"
#include <tuple>
#include <vector>

namespace MCP::Reference {
    using ExtraDataType = RE::ExtraDataType;

    namespace ExtraData {
        struct FormCount {
            TripletID form;
            int32_t count;

            FormCount(const RE::TESForm* f, int32_t c) : form(f), count(c) {
            }

            void to_imgui() const;
        };

        struct RefKeyword {
            TripletID ref;
            TripletID keyword;

            RefKeyword(const RE::TESForm* r, const RE::TESForm* k) : ref(r), keyword(k) {
            }

            void to_imgui() const;
        };

        struct FactionChanges {
            std::vector<std::pair<TripletID, int8_t>> changes;
            TripletID crimeFaction;
            bool removeCrimeFaction{};
            void to_imgui() const;
        };

        struct Action {
            TripletID actionRef;
            int8_t actionType;

            Action(const RE::TESForm* r, int8_t t) : actionRef(r), actionType(t) {
            }

            void to_imgui() const;
        };

        struct Lock {
            int8_t base_level;
            uint8_t flags;
            uint32_t n_tries;
            TripletID key;
            void to_imgui() const;
        };

        struct StartingPosition {
            TripletID location;
            std::pair<RE::NiPoint3, RE::NiPoint3> position_rotation;
            void to_imgui() const;
        };

        struct Package {
            TripletID package;
            int32_t index;
            TripletID target;
            bool actionComplete;
            bool activated;
            bool doneOnce;
            void to_imgui() const;
        };

        void filldata(const RE::ExtraContainerChanges*, std::vector<FormCount>&);
        void filldata(const RE::ExtraLinkedRef*, std::vector<RefKeyword>&);
        void filldata(const RE::ExtraLinkedRefChildren*, std::vector<RefKeyword>&);
        void filldata(const RE::ExtraPromotedRef*, std::vector<TripletID>&);
        void filldata(const RE::ExtraFactionChanges*, std::vector<FactionChanges>&);
        void filldata(const RE::ExtraOwnership*, std::vector<TripletID>&);
        void filldata(const RE::ExtraAction*, std::vector<Action>&);
        void filldata(const RE::ExtraLocationRefType*, std::vector<TripletID>&);
        void filldata(const RE::ExtraLocation*, std::vector<TripletID>&);
        void filldata(const RE::ExtraPackage*, std::vector<Package>&);
        void filldata(const RE::ExtraLock*, std::vector<Lock>&);
        void filldata(const RE::ExtraStartingPosition*, std::vector<StartingPosition>&);

        template <typename T, typename U>
        struct Holder {
            std::vector<U> data;

            void ingest(const RE::BSExtraData* e) {
                if (e && e->GetType() == T::EXTRADATATYPE) filldata(skyrim_cast<const T*>(e), data);
            }

            static RE::ExtraDataType GetType() { return T::EXTRADATATYPE; }
        };
    }

    struct Info {
        TripletID ref, base, location, faction;
        using Holders = std::tuple<
            ExtraData::Holder<RE::ExtraContainerChanges, ExtraData::FormCount>,
            ExtraData::Holder<RE::ExtraLinkedRef, ExtraData::RefKeyword>,
            ExtraData::Holder<RE::ExtraLinkedRefChildren, ExtraData::RefKeyword>,
            ExtraData::Holder<RE::ExtraPromotedRef, TripletID>,
            ExtraData::Holder<RE::ExtraFactionChanges, ExtraData::FactionChanges>,
            ExtraData::Holder<RE::ExtraOwnership, TripletID>,
            ExtraData::Holder<RE::ExtraAction, ExtraData::Action>,
            ExtraData::Holder<RE::ExtraLocationRefType, TripletID>,
            ExtraData::Holder<RE::ExtraLock, ExtraData::Lock>,
            ExtraData::Holder<RE::ExtraLocation, TripletID>,
            ExtraData::Holder<RE::ExtraPackage, ExtraData::Package>,
            ExtraData::Holder<RE::ExtraStartingPosition, ExtraData::StartingPosition>>;
        Holders holders;
        std::vector<RE::ExtraDataType> extradata;
        void FillExtraData();
        explicit Info(RE::TESObjectREFR* r = nullptr);
        explicit operator bool() const noexcept { return ref_.get() != nullptr; }
        bool operator!() const noexcept { return !static_cast<bool>(*this); }

    private:
        RE::NiPointer<RE::TESObjectREFR> ref_ = nullptr;
    };

    extern Info ref_info;
    void UpdateRefInfo(RE::TESObjectREFR* a_refr);
    void PrintRefInfo();
    void PrintExtraData();
    void __stdcall Render();
}