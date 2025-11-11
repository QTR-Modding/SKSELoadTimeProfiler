#pragma once
#include "Events.h"

static void HelpMarker(const char* desc);

namespace MCP {

	inline std::string log_path = Utilities::GetLogPath().string();
    inline std::vector<std::string> logLines;

	void Register();

    void __stdcall RenderLog();

	// forward declare ExtraData namespace
    namespace ExtraData {
        namespace InventoryChanges {
            struct Info {
                std::string form_name;
                int32_t count;
            };
        }
        namespace LinkedRef {
            struct Info {
                std::string ref_name;
				RE::FormID refid;
                std::string keyword;
            };
        }
        namespace PromotedRef {
            struct Info {
                std::string ref_name;
				RE::FormID refid;
            };
        }
    }


    namespace Reference {
        void __stdcall Render();
        inline std::string reference;
        struct Info {
            RE::NiPointer<RE::TESObjectREFR> ref = nullptr;
            RE::FormID refid = 0;
            std::string name;
            RE::BGSLocation* location = nullptr;
			std::string location_name;
			RE::TESFaction* faction = nullptr;
            std::string faction_name;

			RE::TESObjectREFR* merchantChest = nullptr;
            std::string merchantChest_name;


            std::vector<RE::ExtraDataType> extradata;

            std::vector<ExtraData::InventoryChanges::Info> inventory_changes;
			std::vector<ExtraData::LinkedRef::Info> linked_refs;
			std::vector<ExtraData::PromotedRef::Info> promoted_refs;
        };
		inline Info ref_info;

        void UpdateRefInfo(RE::TESObjectREFR* a_refr);
        void PrintRefInfo();
        void PrintExtraData();
    }
};