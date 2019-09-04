// Copyright (c) 2019 Lordmau5
#include "plugin.h"
#include "common.h"

#include <filesystem>
#include <queue>
#include <thread>

#include "effects/abstract/Autosave.h"
#include "effects/abstract/EffectPlaceholder.h"
#include "effects/abstract/FunctionEffect.h"
#include "effects/cheats/CheatHandler.h"
#include "effects/impl/EffectHandler.h"
#include "effects/impl/HookHandler.h"
#include "util/DrawHelper.h"
#include "util/DrawVoting.h"
#include "util/GenericUtil.h"
#include "util/RandomHelper.h"

#include "effects/other/Ped.h"
#include "effects/other/Teleportation.h"
#include "effects/other/Vehicle.h"
#include "effects/other/Wanted.h"

#include "CCheat.h"
#include "CGenericGameStorage.h"
#include "CStats.h"
#include "CText.h"
#include "CTheScripts.h"
#include "CWeather.h"

// Version 1.1 Beta

using namespace plugin;

enum EffectState {
	WEATHER,
	SPAWN_VEHICLE,
	CHEAT,
	TIMED_CHEAT,
	EFFECT,
	TIMED_EFFECT,
	TELEPORT,
	OTHER,

	TEXT,
	TIME,
	BIG_TEXT,
	SET_SEED,
	CRYPTIC_EFFECTS,
	VOTES
};

static CdeclEvent<AddressList<0x53E83C, H_CALL, 0x53EBA2, H_CALL>, PRIORITY_AFTER, ArgPickNone, void()> onDrawAfterFade;

class GTASAChaosMod {
public:
	std::queue<std::function<void()>> queue;
	int remaining = 0;
	int lastSaved = 0;

	int lastMissionsPassed = -1;
	bool* onMission = reinterpret_cast<bool*>(0xA49FC4);

	std::list<TimedEffect*> activeEffects;

	void ProcessEvents() {
		EmptyQueue();

		for (TimedEffect* effect : activeEffects) {
			effect->TickDown();
		}
		activeEffects.remove_if([](TimedEffect* effect) { return !effect->IsRunning(); });

		CCheat::m_bHasPlayerCheated = false;

		HandleAutoSave();
	}

	void EmptyQueue() {
		if (!queue.empty()) {
			queue.front()();
			queue.pop();
		}
	}

	bool IsAnyMissionScriptActive() {
		for (auto i = CTheScripts::pActiveScripts; i = i->m_pNext;) {
			if (i->m_bIsMission) {
				return true;
			}
		}

		return false;
	}

	void HandleAutoSave() {
		int missionsPassed = GenericUtil::GetRealMissionsPassed();
		int currentTime = CTimer::m_snTimeInMilliseconds;

		if (lastMissionsPassed == -1) {
			lastMissionsPassed = missionsPassed;
		}

		if (!*onMission && missionsPassed > lastMissionsPassed && lastSaved < currentTime && !IsAnyMissionScriptActive()) {
			lastMissionsPassed = missionsPassed;

			gtaSAChaosMod.QueueEffect(new Autosave(missionsPassed));

			lastSaved = currentTime + 1000;
		}
	}

	void QueueEffect(TimedEffect* effect) {
		if (effect == nullptr) {
			return;
		}

		// If an effect with the same type or description is found, disable it, disable it
		auto it = std::find_if(activeEffects.begin(), activeEffects.end(), [effect](TimedEffect* _effect) { return effect->IsEqualType(_effect) || effect->IsEqualDescription(_effect); });
		if (it != activeEffects.end()) {
			TimedEffect* _effect = *it;
			if (_effect) {
				_effect->Disable();
			}
		}
		activeEffects.push_front(effect);
	}

	void DrawRemainingTime() {
		DrawHelper::DrawRemainingTimeRects(remaining);
		if (!FrontEndMenuManager.m_bMenuActive) {
			DrawHelper::DrawRecentEffects(activeEffects);
			DrawHelper::DrawMessages();
			DrawHelper::DrawBigMessages();
			DrawVoting::DrawVotes();
		}
	}

	template<typename _Callable, typename... _Args>
	void QueueFunction(_Callable&& __f, _Args&& ... __args) {
		queue.push(std::bind(std::forward<_Callable>(__f), std::forward<_Args>(__args)...));
	}

	void CallFunction(std::string text) {
		char c_state[32];
		char c_function[512];
		int duration;
		char c_voter[128];
		char c_description[128];
		int rapid_fire;
		sscanf(text.c_str(), "%[^:]:%[^:]:%d:%[^:]:%[^:]:%d", &c_state, &c_function, &duration, &c_description, &c_voter, &rapid_fire);

		if (rapid_fire == 1) {
			duration = 3000;
		}

		std::string state(c_state);
		std::string function(c_function);
		std::string description(c_description);
		std::string voter(c_voter);

		EffectState currentState;
		if (state == "weather") {
			currentState = EffectState::WEATHER;
		}
		else if (state == "spawn_vehicle") {
			currentState = EffectState::SPAWN_VEHICLE;
		}
		else if (state == "cheat") {
			currentState = EffectState::CHEAT;
		}
		else if (state == "timed_cheat") {
			currentState = EffectState::TIMED_CHEAT;
		}
		else if (state == "effect") {
			currentState = EffectState::EFFECT;
		}
		else if (state == "timed_effect") {
			currentState = EffectState::TIMED_EFFECT;
		}
		else if (state == "other") {
			currentState = EffectState::OTHER;
		}
		else if (state == "teleport") {
			currentState = EffectState::TELEPORT;
		}
		else if (state == "text") {
			currentState = EffectState::TEXT;
		}
		else if (state == "time") {
			currentState = EffectState::TIME;
		}
		else if (state == "big_text") {
			currentState = EffectState::BIG_TEXT;
		}
		else if (state == "set_seed") {
			currentState = EffectState::SET_SEED;
		}
		else if (state == "cryptic_effects") {
			currentState = EffectState::CRYPTIC_EFFECTS;
		}
		else if (state == "votes") {
			currentState = EffectState::VOTES;
		}
		else {
			return;
		}

		switch (currentState) {
			case EffectState::WEATHER: {
				QueueEffect((new EffectPlaceholder(duration, description))->SetVoter(voter)->SetRapidFire(rapid_fire));
				QueueFunction(CWeather::ForceWeatherNow, std::stoi(function));

				break;
			}
			case EffectState::SPAWN_VEHICLE: {
				int modelID = std::stoi(function);
				QueueEffect((new EffectPlaceholder(duration, description))->SetVoter(voter)->SetRapidFire(rapid_fire));
				QueueFunction(Vehicle::SpawnForPlayer, modelID);

				break;
			}
			case EffectState::CHEAT:
			case EffectState::TIMED_CHEAT: {
				QueueEffect(CheatHandler::HandleCheat(function, duration, description)->SetVoter(voter)->SetRapidFire(rapid_fire));

				break;
			}
			case EffectState::EFFECT:
			case EffectState::TIMED_EFFECT: {
				QueueEffect(EffectHandler::HandleEffect(function, duration, description)->SetVoter(voter)->SetRapidFire(rapid_fire));

				break;
			}
			case EffectState::TELEPORT: {
				int x, y, z;
				sscanf(function.c_str(), "%d,%d,%d", &x, &y, &z);

				QueueEffect((new EffectPlaceholder(duration, description))->SetVoter(voter)->SetRapidFire(rapid_fire));
				QueueFunction(Teleportation::Teleport, CVector((float)x, (float)y, (float)z));

				break;
			}
			case EffectState::OTHER: {
				if (function == "clear_active_effects") {
					QueueFunction([this, duration, description, voter, rapid_fire] {
						for (TimedEffect* effect : activeEffects) {
							effect->Disable();
						}
						QueueEffect((new EffectPlaceholder(duration, description))->SetVoter(voter)->SetRapidFire(rapid_fire));
					});
				}

				break;
			}
			case EffectState::TEXT: {
				QueueFunction(DrawHelper::DrawHelpMessage, description, 5000);

				break;
			}
			case EffectState::TIME: {
				remaining = std::stoi(function);

				break;
			}
			case EffectState::BIG_TEXT: {
				QueueFunction(DrawHelper::DrawBigMessage, function, 10000);

				break;
			}
			case EffectState::SET_SEED: {
				RandomHelper::SetSeed(std::stoi(function));

				break;
			}
			case EffectState::CRYPTIC_EFFECTS: {
				GenericUtil::areEffectsCryptic = std::stoi(function);

				break;
			}
			case EffectState::VOTES: {
				char c_effects[3][128];
				int c_votes[3];
				int pickedChoice = -1;
				sscanf(function.c_str(), "%[^;];%d;;%[^;];%d;;%[^;];%d;;%d",
					&c_effects[0], &c_votes[0],
					&c_effects[1], &c_votes[1],
					&c_effects[2], &c_votes[2],
					&pickedChoice
				);

				DrawVoting::UpdateVotes(c_effects, c_votes, pickedChoice);

				break;
			}
			default: {
				break;
			}
		}

		return;
	}

	void OnAttach() {
		HANDLE hPipe;
		char buffer[1024] = "";
		char text[1024] = "";
		DWORD dwRead;

		std::string myProcID = "\\\\.\\pipe\\GTASAChaosPipe";
		std::string To(myProcID.begin(), myProcID.end());

		hPipe = CreateNamedPipe(To.c_str(),
			PIPE_ACCESS_DUPLEX | PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
			PIPE_WAIT,
			1,
			1024 * 16,
			1024 * 16,
			NMPWAIT_USE_DEFAULT_WAIT,
			NULL);
		while (hPipe != NULL)
		{
			if (ConnectNamedPipe(hPipe, NULL) != FALSE)
			{
				while (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &dwRead, NULL) != FALSE)
				{
					for (unsigned int i = 0; i < strlen(buffer); i++)
					{
						if (isprint(buffer[i]) == false)
							break;

						text[i] = buffer[i];
					}
					CallFunction(std::string(text));
					memset(text, 0, sizeof(text));
				}
				FlushFileBuffers(hPipe);
			}
			DisconnectNamedPipe(hPipe);
		}
	}

	void SetupPipe() {
		std::thread t1([](GTASAChaosMod* chaos) { chaos->OnAttach(); }, this);
		t1.detach();
	}

	// Overwrite OpenFile so we can route the 9th slot to our custom chaos_autosave file
	static int __cdecl HookedOpenFile(const char* path, const char* mode) {
		std::string s_path(path),
			save_path = "GTASAsf9.b";

		size_t start_pos = s_path.find(save_path);
		if (start_pos != std::string::npos) {
			s_path.replace(start_pos, save_path.length(), GenericUtil::GetLoadFileName().c_str());
		}

		return CFileMgr::OpenFile(s_path.c_str(), mode);
	}

	GTASAChaosMod() {
		SetupPipe();

		GenericUtil::InitializeCharReplacements();
		GenericUtil::InitializeUnprotectedMemory();
		HookHandler::Initialize();

		patch::RedirectCall(0x5D0D66, HookedOpenFile);

		// Disable Replays
		patch::Nop(0x53C090, 5);

		// Disable Interior Music
		patch::Nop(0x508450, 6);
		patch::Nop(0x508817, 6);

		// Fix Reefer w/ Invisible Cars
		patch::Nop(0x6F14DE, 3);

		Events::gameProcessEvent.Add([this] { this->ProcessEvents(); });

		onDrawAfterFade.AddAfter([this] { this->DrawRemainingTime(); });
	}
} gtaSAChaosMod;
