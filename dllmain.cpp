// dllmain.cpp : Defines the entry point for the DLL application.
#include <Windows.h>
#include <d3d9.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <sstream>


#include "arcdps_structs.h"
#include "global.h"
#include "imgui/imgui.h"
#include "KillproofUI.h"
#include "Player.h"
#include "Settings.h"
#include "SettingsUI.h"

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD ul_reason_for_call,
                      LPVOID lpReserved
) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

// predefine some functions
void readArcExports();

// globals
char* arcvers;
arcdps_exports arc_exports = {};
bool show_settings = false;
SettingsUI settingsUi;

// load arcdps dll.
// When loading directly, arcdps is the "d3d9.dll"
// When loading with the addon manager, arcdps is called "gw2addon_arcdps.dll"
HMODULE arc_dllD = LoadLibraryA("d3d9.dll");
HMODULE arc_dllL = LoadLibraryA("gw2addon_arcdps.dll");
HMODULE arc_dll = (arc_dllL != nullptr) ? arc_dllL : arc_dllD;

typedef uint64_t (*arc_export_func_u64)();
// arc options
auto arc_export_e6 = (arc_export_func_u64)GetProcAddress(arc_dll, "e6");
bool arc_hide_all = false;
bool arc_panel_always_draw = false;
bool arc_movelock_altui = false;
bool arc_clicklock_altui = false;
bool arc_window_fastclose = false;

// arc keyboard modifier
auto arc_export_e7 = (arc_export_func_u64)GetProcAddress(arc_dll, "e7");
DWORD arc_global_mod1 = 0;
DWORD arc_global_mod2 = 0;
DWORD arc_global_mod_multi = 0;

// arc add to log
e3_func_ptr arc_log = (e3_func_ptr)GetProcAddress(arc_dll, "e3");

/* window callback -- return is assigned to umsg (return zero to not be processed by arcdps or game) */
uintptr_t mod_wnd(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	try {
		auto const io = &ImGui::GetIO();

		switch (uMsg) {
		case WM_KEYUP:
		case WM_SYSKEYUP:
			{
				const int vkey = (int)wParam;
				io->KeysDown[vkey] = false;
				if (vkey == VK_CONTROL) {
					io->KeyCtrl = false;
				} else if (vkey == VK_MENU) {
					io->KeyAlt = false;
				} else if (vkey == VK_SHIFT) {
					io->KeyShift = false;
				}
				break;
			}
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			{
				readArcExports();
				const int vkey = (int)wParam;
				// close windows on escape press (return 0, so arc and gw2 are not processing this event)
				Settings& settings = Settings::instance();
				bool& show_killproof = settings.getShowKillproof();
				if (!arc_hide_all && vkey == VK_ESCAPE) {
					// close settings menu first
					if (arc_window_fastclose && show_settings) {
						show_settings = false;
						return 0;
					}
					// close killproof window with escape
					if (arc_window_fastclose && !settings.getDisableEscClose() && show_killproof) {
						show_killproof = false;
						return 0;
					}
				}
				// toggle killproof window
				if (io->KeysDown[arc_global_mod1] && io->KeysDown[arc_global_mod2] && vkey == settings.getKillProofKey() && !arc_hide_all) {
					show_killproof = !show_killproof;
					return 0;
				}
				if (vkey == VK_CONTROL) {
					io->KeyCtrl = true;
				} else if (vkey == VK_MENU) {
					io->KeyAlt = true;
				} else if (vkey == VK_SHIFT) {
					io->KeyShift = true;
				}
				io->KeysDown[vkey] = true;
				break;
			}
		case WM_ACTIVATEAPP:
			{
				if (!wParam) {
					io->KeysDown[arc_global_mod1] = false;
					io->KeysDown[arc_global_mod2] = false;
				}
				break;
			}
		default:
			break;
		}
	} catch (const std::exception& e) {
		arc_log(const_cast<char*>(e.what()));
		throw e;
	}

	return uMsg;
}

/* combat callback -- may be called asynchronously. return ignored */
/* one participant will be party/squad, or minion of. no spawn statechange events. despawn statechange only on marked boss npcs */
uintptr_t mod_combat(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision) {
	try {
		/* ev is null. dst will only be valid on tracking add. skillname will also be null */
		if (!ev) {
			/* notify tracking change */
			if (!src->elite) {
				// only run, when names are set and not null
				if (src->name != nullptr && src->name[0] != '\0' && dst->name != nullptr && dst->name[0] != '\0') {

					std::string username(dst->name);

					// remove ':' at the beginning of the name.
					if (username.at(0) == ':') {
						username.erase(0, 1);
					}

					/* add */
					if (src->prof) {
						std::scoped_lock lock(cachedPlayersMutex, trackedPlayersMutex);

						auto playerIt = cachedPlayers.find(username);
						if (playerIt == cachedPlayers.end()) {
							// no element found, create it
							const auto& tryEmplace = cachedPlayers.try_emplace(username, username, src->name);

							// check if emplacing successful, if yes, load the kp.me page
							if (tryEmplace.second) {
								// save player object to work on
								Player& player = tryEmplace.first->second;

								// load killproofs
								player.loadKillproofs();
							}
						}
						else {
							// update charactername
							playerIt->second.characterName = src->name;
						}

						// add to tracking
						trackedPlayers.emplace_back(username);

						// Tell the UI to resort, cause we added a player
						killproofUi.needSort = true;
					}
					/* remove */
					else {
						std::lock_guard<std::mutex> guard(trackedPlayersMutex);
						trackedPlayers.erase(std::remove(trackedPlayers.begin(), trackedPlayers.end(), username), trackedPlayers.end());
					}
				}
			}
		}
	} catch (const std::exception& e) {
		arc_log(const_cast<char*>(e.what()));

		// create dump of params
		if (ev) {
			arc_log(const_cast<char*>("ev:\n"));
			char event[sizeof(cbtevent)];
			memcpy(event, ev, sizeof(cbtevent));
			std::stringstream evss;
			for (size_t i = 0; i < sizeof(cbtevent); ++i) {
				evss << std::hex << (int)event[i];
			}
			evss << "\n";
			std::string evs = evss.str();
			arc_log(const_cast<char*>(evs.c_str()));
		}

		if (src) {
			arc_log(const_cast<char*>("src:\n"));
			char srcData[sizeof(ag)];
			memcpy(srcData, src, sizeof(ag));
			std::stringstream srcDatass;
			for (size_t i = 0; i < sizeof(ag); ++i) {
				srcDatass << std::hex << (int)srcData[i];
			}
			srcDatass << "\n";
			std::string srcDataS = srcDatass.str();
			arc_log(const_cast<char*>(srcDataS.c_str()));

			if (src->name) {
				arc_log(const_cast<char*>("src->name:\n"));
				arc_log(src->name);
			}
		}

		if (dst) {
			arc_log(const_cast<char*>("dst:\n"));
			char dstData[sizeof(ag)];
			memcpy(dstData, dst, sizeof(ag));
			std::stringstream dstDatass;
			for (size_t i = 0; i < sizeof(ag); ++i) {
				dstDatass << std::hex << (int)dstData[i];
			}
			dstDatass << "\n";
			std::string dstDataS = dstDatass.str();
			arc_log(const_cast<char*>(dstDataS.c_str()));

			if (dst->name) {
				arc_log(const_cast<char*>("dst->name:\n"));
				arc_log(dst->name);
			}
		}
		throw e;
	}
	return 0;
}

uintptr_t mod_options() {
	if (ImGui::BeginMenu("Killproof.me")) {
		bool& showKillproof = Settings::instance().getShowKillproof();
		ImGui::Checkbox("Killproofs", &showKillproof);
		ImGui::Checkbox("Settings", &show_settings);
		ImGui::EndMenu();
	}

	return 0;
}

bool modsPressed() {
	auto io = &ImGui::GetIO();

	return io->KeysDown[arc_global_mod1] && io->KeysDown[arc_global_mod2];
}

bool canMoveWindows() {
	if (!arc_movelock_altui) {
		return true;
	} else {
		return modsPressed();
	}
}

void ShowKillproof(bool* p_open) {
	if (*p_open) {
		killproofUi.draw("Killproof.me", p_open,
		                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | (!canMoveWindows() ? ImGuiWindowFlags_NoMove : 0));
	}
}

void ShowSettings(bool* p_open) {
	if (show_settings) {
		settingsUi.draw("Killproof.me Settings", p_open,
		                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | (!canMoveWindows() ? ImGuiWindowFlags_NoMove : 0));
	}
}

void readArcExports() {
	uint64_t e6_result = arc_export_e6();
	uint64_t e7_result = arc_export_e7();

	arc_hide_all = (e6_result & 0x01);
	arc_panel_always_draw = (e6_result & 0x02);
	arc_movelock_altui = (e6_result & 0x04);
	arc_clicklock_altui = (e6_result & 0x08);
	arc_window_fastclose = (e6_result & 0x10);


	uint16_t* ra = (uint16_t*)&e7_result;
	if (ra) {
		arc_global_mod1 = ra[0];
		arc_global_mod2 = ra[1];
		arc_global_mod_multi = ra[2];
	}
}

uintptr_t mod_imgui(uint32_t not_charsel_or_loading) {
	try {
		if (!not_charsel_or_loading) return 0;
		bool& showKillproof = Settings::instance().getShowKillproof();
		ShowKillproof(&showKillproof);
		ShowSettings(&show_settings);
	} catch (const std::exception& e) {
		arc_log(const_cast<char*>(e.what()));
		throw e;
	}

	return 0;
}


/* initialize mod -- return table that arcdps will use for callbacks */
arcdps_exports* mod_init() {
	/* for arcdps */
	arc_exports.sig = 0x6BAF1938;
	arc_exports.imguivers = IMGUI_VERSION_NUM;
	arc_exports.size = sizeof(arcdps_exports);
	arc_exports.out_name = "killproof.me";
	arc_exports.out_build = "2.0.0-beta";
	arc_exports.wnd_nofilter = mod_wnd;
	arc_exports.combat = mod_combat;
	arc_exports.imgui = mod_imgui;
	arc_exports.options_end = mod_options;
	//arc_exports.size = (uintptr_t)"error message if you decide to not load, sig must be 0";
	return &arc_exports;
}

/* export -- arcdps looks for this exported function and calls the address it returns on client load */
extern "C" __declspec(dllexport) void* get_init_addr(char* arcversionstr, void* imguicontext, void* id3dd9, HANDLE arcdll, void* mallocfn, void* freefn) {
	arcvers = arcversionstr;
	ImGui::SetCurrentContext(static_cast<ImGuiContext*>(imguicontext));
	ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))mallocfn, (void (*)(void*, void*))freefn);
	return mod_init;
}

/* release mod -- return ignored */
uintptr_t mod_release() {
	return 0;
}

/* export -- arcdps looks for this exported function and calls the address it returns on client exit */
extern "C" __declspec(dllexport) void* get_release_addr() {
	arcvers = nullptr;
	return mod_release;
}
