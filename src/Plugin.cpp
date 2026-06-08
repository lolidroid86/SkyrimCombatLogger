#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <SimpleIni.h>
#include <sstream>
#include <unordered_set>

using namespace std::literals;

SKSEPluginInfo(
    .Version = { 1, 0, 0, 0 },
    .Name    = "SkyrimCombatLogger",
    .Author  = "lolidroid86"
);

// ---- Config ----------------------------------------------------------------

struct Config {
    bool  logHits                    = true;
    bool  logCombat                  = true;
    bool  logDeaths                  = true;
    bool  logFactions                = true;
    bool  logMgefName                = true;
    bool  onlyFollowers              = false;
    bool  blockFollowerHits          = true;
    bool  resetFollowerCombatOnLoad  = true;
    std::unordered_set<RE::FormID> mgefExclude;
};

static Config g_cfg;

// ---- Log setup -------------------------------------------------------------

static std::shared_ptr<spdlog::logger> g_log;

static void SetupLog()
{
    auto path = SKSE::log::log_directory();
    if (!path) return;

    *path /= "SkyrimCombatLogger.log";

    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
    g_log = std::make_shared<spdlog::logger>("CombatLogger", sink);
    g_log->set_pattern("[%H:%M:%S] %v");
    g_log->set_level(spdlog::level::trace);
    g_log->flush_on(spdlog::level::trace);
}

static void CLog(std::string_view msg)
{
    if (g_log) g_log->info("{}", msg);
}

// ---- Helpers ---------------------------------------------------------------

static RE::TESFaction* GetFollowerFaction()
{
    static RE::TESFaction* faction = nullptr;
    if (!faction) {
        faction = RE::TESForm::LookupByID<RE::TESFaction>(0x05C84E);
    }
    return faction;
}

static bool IsFollower(RE::Actor* actor)
{
    if (!actor) return false;
    if (actor->IsPlayerTeammate()) return true;
    auto* fac = GetFollowerFaction();
    if (!fac) return false;
    return actor->IsInFaction(fac);
}

static std::string ActorLabel(RE::TESObjectREFR* ref)
{
    if (!ref) return "None(null)";

    auto* actor = ref->As<RE::Actor>();
    std::string name = ref->GetName();
    if (name.empty()) name = "Unnamed";

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s(%08X)", name.c_str(), ref->GetFormID());

    if (actor && g_cfg.logFactions && IsFollower(actor)) {
        return std::string(buf) + "[F]";
    }
    return buf;
}

static std::string MgefLabel(RE::FormID formID)
{
    if (!formID) return "";

    auto* form = RE::TESForm::LookupByID(formID);
    if (!form) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "MGEF(%08X)", formID);
        return buf;
    }

    std::string edid;
    if (g_cfg.logMgefName) {
        edid = form->GetFormEditorID();
    }

    char buf[128];
    if (!edid.empty()) {
        std::snprintf(buf, sizeof(buf), "%s(%08X)", edid.c_str(), formID);
    } else {
        std::snprintf(buf, sizeof(buf), "MGEF(%08X)", formID);
    }
    return buf;
}

static std::string SourceLabel(RE::FormID sourceFormID, [[maybe_unused]] RE::FormID projFormID)
{
    if (sourceFormID) {
        auto* src = RE::TESForm::LookupByID(sourceFormID);
        if (src) {
            if (auto* mgef = src->As<RE::EffectSetting>()) {
                return MgefLabel(sourceFormID);
            }
            if (auto* weap = src->As<RE::TESObjectWEAP>()) {
                std::string name = weap->GetName();
                if (name.empty()) name = "UnnamedWeapon";
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s(%08X)", name.c_str(), sourceFormID);
                return buf;
            }
            if (auto* spell = src->As<RE::SpellItem>()) {
                std::string name = spell->GetName();
                if (name.empty()) name = "UnnamedSpell";
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s(%08X)", name.c_str(), sourceFormID);
                return buf;
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Form%02X(%08X)", (int)src->GetFormType(), sourceFormID);
            return buf;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Unknown(%08X)", sourceFormID);
        return buf;
    }
    return "NoSource";
}

// ---- Follower combat reset -------------------------------------------------

static void ResetFollowerCombat(RE::Actor* a, RE::Actor* b)
{
    auto ha = a->GetHandle();
    auto hb = b->GetHandle();

    SKSE::GetTaskInterface()->AddTask([ha, hb]() {
        if (auto pa = ha.get()) {
            pa->StopCombat();
            pa->StopAlarmOnActor();
        }
        if (auto pb = hb.get()) {
            pb->StopCombat();
            pb->StopAlarmOnActor();
        }
    });
}

static void ResetAllFollowerCombat()
{
    auto* pl = RE::ProcessLists::GetSingleton();
    if (!pl) return;

    std::vector<RE::ActorHandle> handles;
    for (auto& h : pl->highActorHandles) {
        auto smart = h.get();
        if (smart && IsFollower(smart.get()))
            handles.push_back(h);
    }

    for (auto& h : handles) {
        if (auto actor = h.get()) {
            actor->StopCombat();
            actor->StopAlarmOnActor();
        }
    }

    char buf[128];
    std::snprintf(buf, sizeof(buf), "[RESET] post-load combat sweep: %zu followers cleared", handles.size());
    CLog(buf);
}

// ---- Event sinks -----------------------------------------------------------

class HitEventSink : public RE::BSTEventSink<RE::TESHitEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESHitEvent* event,
        RE::BSTEventSource<RE::TESHitEvent>*) override
    {
        if (!event || !g_cfg.logHits) return RE::BSEventNotifyControl::kContinue;

        auto* target = event->target.get();
        auto* cause  = event->cause.get();

        auto* tActor = target ? target->As<RE::Actor>() : nullptr;
        auto* cActor = cause  ? cause->As<RE::Actor>()  : nullptr;

        if (g_cfg.onlyFollowers) {
            if (!IsFollower(tActor) && !IsFollower(cActor)) {
                return RE::BSEventNotifyControl::kContinue;
            }
        }

        std::string src = SourceLabel(event->source, event->projectile);

        char buf[512];
        std::snprintf(buf, sizeof(buf), "[HIT] %s → %s | via %s",
            ActorLabel(cause).c_str(),
            ActorLabel(target).c_str(),
            src.c_str());
        CLog(buf);

        return RE::BSEventNotifyControl::kContinue;
    }

    static HitEventSink* GetSingleton()
    {
        static HitEventSink sink;
        return &sink;
    }
};

class CombatEventSink : public RE::BSTEventSink<RE::TESCombatEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCombatEvent* event,
        RE::BSTEventSource<RE::TESCombatEvent>*) override
    {
        if (!event || !g_cfg.logCombat) return RE::BSEventNotifyControl::kContinue;

        auto* actor  = event->actor.get();
        auto* target = event->targetActor.get();

        if (g_cfg.onlyFollowers) {
            auto* aActor = actor  ? actor->As<RE::Actor>()  : nullptr;
            auto* tActor = target ? target->As<RE::Actor>() : nullptr;
            if (!IsFollower(aActor) && !IsFollower(tActor)) {
                return RE::BSEventNotifyControl::kContinue;
            }
        }

        const char* stateStr = "unknown";
        switch (*event->newState) {
            case RE::ACTOR_COMBAT_STATE::kNone:       stateStr = "exited combat"; break;
            case RE::ACTOR_COMBAT_STATE::kCombat:     stateStr = "entered combat"; break;
            case RE::ACTOR_COMBAT_STATE::kSearching:  stateStr = "searching"; break;
        }

        char buf[512];
        std::snprintf(buf, sizeof(buf), "[COMBAT] %s %s with %s",
            ActorLabel(actor).c_str(),
            stateStr,
            ActorLabel(target).c_str());
        CLog(buf);

        if (g_cfg.blockFollowerHits &&
            *event->newState == RE::ACTOR_COMBAT_STATE::kCombat)
        {
            auto* aActor = actor  ? actor->As<RE::Actor>()  : nullptr;
            auto* tActor = target ? target->As<RE::Actor>() : nullptr;
            if (aActor && tActor && IsFollower(aActor) && IsFollower(tActor)) {
                CLog("[BLOCK] follower-vs-follower combat entry — queuing reset");
                ResetFollowerCombat(aActor, tActor);
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    static CombatEventSink* GetSingleton()
    {
        static CombatEventSink sink;
        return &sink;
    }
};

class DeathEventSink : public RE::BSTEventSink<RE::TESDeathEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESDeathEvent* event,
        RE::BSTEventSource<RE::TESDeathEvent>*) override
    {
        if (!event || !event->dead) return RE::BSEventNotifyControl::kContinue;

        auto* dead   = event->actorDying.get();
        auto* killer = event->actorKiller.get();
        auto* dActor = dead   ? dead->As<RE::Actor>()   : nullptr;
        auto* kActor = killer ? killer->As<RE::Actor>() : nullptr;

        if (g_cfg.logDeaths) {
            bool logThis = !g_cfg.onlyFollowers || IsFollower(dActor) || IsFollower(kActor);
            if (logThis) {
                char buf[512];
                std::snprintf(buf, sizeof(buf), "[DEATH] %s killed by %s",
                    ActorLabel(dead).c_str(),
                    ActorLabel(killer).c_str());
                CLog(buf);
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    static DeathEventSink* GetSingleton()
    {
        static DeathEventSink sink;
        return &sink;
    }
};

class MagicEffectApplySink : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESMagicEffectApplyEvent* event,
        RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
    {
        if (!event || !g_cfg.logHits) return RE::BSEventNotifyControl::kContinue;
        if (!g_cfg.mgefExclude.empty() && g_cfg.mgefExclude.count(event->magicEffect))
            return RE::BSEventNotifyControl::kContinue;

        auto* target = event->target.get();
        auto* caster = event->caster.get();

        if (g_cfg.onlyFollowers) {
            auto* tActor = target ? target->As<RE::Actor>() : nullptr;
            auto* cActor = caster ? caster->As<RE::Actor>() : nullptr;
            if (!IsFollower(tActor) && !IsFollower(cActor)) {
                return RE::BSEventNotifyControl::kContinue;
            }
        }

        auto* mgef = RE::TESForm::LookupByID<RE::EffectSetting>(event->magicEffect);
        if (mgef) {
            bool hostile     = mgef->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kHostile);
            bool detrimental = mgef->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kDetrimental);
            if (!hostile && !detrimental) {
                return RE::BSEventNotifyControl::kContinue;
            }
        }

        char buf[512];
        std::snprintf(buf, sizeof(buf), "[MGEF] %s → %s | %s",
            ActorLabel(caster).c_str(),
            ActorLabel(target).c_str(),
            MgefLabel(event->magicEffect).c_str());
        CLog(buf);

        return RE::BSEventNotifyControl::kContinue;
    }

    static MagicEffectApplySink* GetSingleton()
    {
        static MagicEffectApplySink sink;
        return &sink;
    }
};

// ---- INI config ------------------------------------------------------------

static void LoadConfig()
{
    auto iniPath = std::filesystem::path("Data/SKSE/Plugins/SkyrimCombatLogger.ini");

    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(iniPath.string().c_str());

    g_cfg.logHits                    = ini.GetBoolValue("Log",    "LogHits",                   true);
    g_cfg.logCombat                  = ini.GetBoolValue("Log",    "LogCombat",                 true);
    g_cfg.logDeaths                  = ini.GetBoolValue("Log",    "LogDeaths",                 true);
    g_cfg.logFactions                = ini.GetBoolValue("Log",    "LogFactions",               true);
    g_cfg.logMgefName                = ini.GetBoolValue("Log",    "LogMGEFName",               true);
    g_cfg.onlyFollowers              = ini.GetBoolValue("Log",    "OnlyFollowers",             false);
    g_cfg.blockFollowerHits          = ini.GetBoolValue("Combat", "BlockFollowerHits",         true);
    g_cfg.resetFollowerCombatOnLoad  = ini.GetBoolValue("Combat", "ResetFollowerCombatOnLoad", true);

    g_cfg.mgefExclude.clear();
    const char* excludeRaw = ini.GetValue("Filter", "ExcludeMGEF", "");
    if (excludeRaw && *excludeRaw) {
        std::istringstream ss(excludeRaw);
        std::string token;
        while (std::getline(ss, token, ',')) {
            token.erase(0, token.find_first_not_of(" \t"));
            auto end = token.find_last_not_of(" \t");
            if (end != std::string::npos) token.erase(end + 1);
            if (!token.empty()) {
                try {
                    g_cfg.mgefExclude.insert(static_cast<RE::FormID>(std::stoul(token, nullptr, 16)));
                } catch (...) {}
            }
        }
    }
}

// ---- SKSE entry points -----------------------------------------------------

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SetupLog();
    LoadConfig();

    CLog("SkyrimCombatLogger v1.0.0 loaded");

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            auto* src = RE::ScriptEventSourceHolder::GetSingleton();
            if (!src) {
                CLog("ERROR: ScriptEventSourceHolder is null -- no events registered");
                return;
            }
            src->AddEventSink(HitEventSink::GetSingleton());
            src->AddEventSink(CombatEventSink::GetSingleton());
            src->AddEventSink(DeathEventSink::GetSingleton());
            src->AddEventSink(MagicEffectApplySink::GetSingleton());
            CLog("Event sinks registered: Hit / Combat / Death / MagicEffectApply");
        }
        if (msg->type == SKSE::MessagingInterface::kPostLoadGame) {
            if (g_cfg.resetFollowerCombatOnLoad) {
                SKSE::GetTaskInterface()->AddTask([]() {
                    ResetAllFollowerCombat();
                });
            }
        }
    });

    return true;
}
