#ifndef _INCLUDE_ADMIN_FORWARDS_H_
#define _INCLUDE_ADMIN_FORWARDS_H_

#include <functional>
#include <vector>
#include <string>
#include <cstdint>

// Other Metamod plugins can acquire this interface via:
//   ICS2AdminForwards *fwd = (ICS2AdminForwards *)g_SMAPI->MetaFactory(
//       CS2ADMIN_FORWARDS_INTERFACE, nullptr, nullptr);
#define CS2ADMIN_FORWARDS_INTERFACE "ICS2AdminForwards002"

// Forward callback types.
// Returning true from blockable callbacks prevents the action.

// Ban/Unban
using OnBanPlayerFn = std::function<bool(int targetSlot, int adminSlot, int timeMinutes, const char *reason)>;
using OnUnbanPlayerFn = std::function<void(const char *authid, int adminSlot)>;

// Mute/Gag/Silence
using OnMutePlayerFn = std::function<bool(int targetSlot, int adminSlot, int timeMinutes, const char *reason)>;
using OnGagPlayerFn = std::function<bool(int targetSlot, int adminSlot, int timeMinutes, const char *reason)>;
using OnSilencePlayerFn = std::function<bool(int targetSlot, int adminSlot, int timeMinutes, const char *reason)>;
using OnUnmutePlayerFn = std::function<void(int targetSlot, int adminSlot)>;
using OnUngagPlayerFn = std::function<void(int targetSlot, int adminSlot)>;
using OnUnsilencePlayerFn = std::function<void(int targetSlot, int adminSlot)>;

// Kick/Slay (blockable)
using OnKickPlayerFn = std::function<bool(int targetSlot, int adminSlot, const char *reason)>;
using OnSlayPlayerFn = std::function<bool(int targetSlot, int adminSlot)>;

// Map change (blockable)
using OnMapChangeFn = std::function<bool(const char *mapName, int adminSlot)>;

// Player lifecycle
using OnClientConnectedFn = std::function<void(int slot, const char *name, uint64_t steamid64, const char *ip)>;
using OnClientDisconnectFn = std::function<void(int slot)>;
using OnClientAuthorizedFn = std::function<void(int slot, const char *authid, uint64_t steamid64)>;

// Report
using OnReportPlayerFn = std::function<void(int reporterSlot, int targetSlot, const char *reason)>;

// Admin check
using OnClientPreAdminCheckFn = std::function<void(int slot)>;

class ICS2AdminForwards
{
public:
	virtual void RegisterOnBanPlayer(OnBanPlayerFn callback) = 0;
	virtual void RegisterOnUnbanPlayer(OnUnbanPlayerFn callback) = 0;
	virtual void RegisterOnMutePlayer(OnMutePlayerFn callback) = 0;
	virtual void RegisterOnGagPlayer(OnGagPlayerFn callback) = 0;
	virtual void RegisterOnUnmutePlayer(OnUnmutePlayerFn callback) = 0;
	virtual void RegisterOnUngagPlayer(OnUngagPlayerFn callback) = 0;
	virtual void RegisterOnReportPlayer(OnReportPlayerFn callback) = 0;
	virtual void RegisterOnClientPreAdminCheck(OnClientPreAdminCheckFn callback) = 0;

	virtual void RegisterOnKickPlayer(OnKickPlayerFn callback) = 0;
	virtual void RegisterOnSlayPlayer(OnSlayPlayerFn callback) = 0;
	virtual void RegisterOnSilencePlayer(OnSilencePlayerFn callback) = 0;
	virtual void RegisterOnUnsilencePlayer(OnUnsilencePlayerFn callback) = 0;
	virtual void RegisterOnMapChange(OnMapChangeFn callback) = 0;
	virtual void RegisterOnClientConnected(OnClientConnectedFn callback) = 0;
	virtual void RegisterOnClientDisconnect(OnClientDisconnectFn callback) = 0;
	virtual void RegisterOnClientAuthorized(OnClientAuthorizedFn callback) = 0;
};

class CS2AForwards : public ICS2AdminForwards
{
public:
	void RegisterOnBanPlayer(OnBanPlayerFn callback) override { m_onBanPlayer.push_back(callback); }
	void RegisterOnUnbanPlayer(OnUnbanPlayerFn callback) override { m_onUnbanPlayer.push_back(callback); }
	void RegisterOnMutePlayer(OnMutePlayerFn callback) override { m_onMutePlayer.push_back(callback); }
	void RegisterOnGagPlayer(OnGagPlayerFn callback) override { m_onGagPlayer.push_back(callback); }
	void RegisterOnUnmutePlayer(OnUnmutePlayerFn callback) override { m_onUnmutePlayer.push_back(callback); }
	void RegisterOnUngagPlayer(OnUngagPlayerFn callback) override { m_onUngagPlayer.push_back(callback); }
	void RegisterOnReportPlayer(OnReportPlayerFn callback) override { m_onReportPlayer.push_back(callback); }
	void RegisterOnClientPreAdminCheck(OnClientPreAdminCheckFn callback) override { m_onClientPreAdminCheck.push_back(callback); }

	void RegisterOnKickPlayer(OnKickPlayerFn callback) override { m_onKickPlayer.push_back(callback); }
	void RegisterOnSlayPlayer(OnSlayPlayerFn callback) override { m_onSlayPlayer.push_back(callback); }
	void RegisterOnSilencePlayer(OnSilencePlayerFn callback) override { m_onSilencePlayer.push_back(callback); }
	void RegisterOnUnsilencePlayer(OnUnsilencePlayerFn callback) override { m_onUnsilencePlayer.push_back(callback); }
	void RegisterOnMapChange(OnMapChangeFn callback) override { m_onMapChange.push_back(callback); }
	void RegisterOnClientConnected(OnClientConnectedFn callback) override { m_onClientConnected.push_back(callback); }
	void RegisterOnClientDisconnect(OnClientDisconnectFn callback) override { m_onClientDisconnect.push_back(callback); }
	void RegisterOnClientAuthorized(OnClientAuthorizedFn callback) override { m_onClientAuthorized.push_back(callback); }

	// Fire forwards. blockable ones return true if any callback blocked

	bool FireOnBanPlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason)
	{
		for (auto &cb : m_onBanPlayer)
			if (cb(targetSlot, adminSlot, timeMinutes, reason)) return true;
		return false;
	}

	bool FireOnMutePlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason)
	{
		for (auto &cb : m_onMutePlayer)
			if (cb(targetSlot, adminSlot, timeMinutes, reason)) return true;
		return false;
	}

	bool FireOnGagPlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason)
	{
		for (auto &cb : m_onGagPlayer)
			if (cb(targetSlot, adminSlot, timeMinutes, reason)) return true;
		return false;
	}

	bool FireOnKickPlayer(int targetSlot, int adminSlot, const char *reason)
	{
		for (auto &cb : m_onKickPlayer)
			if (cb(targetSlot, adminSlot, reason)) return true;
		return false;
	}

	bool FireOnSlayPlayer(int targetSlot, int adminSlot)
	{
		for (auto &cb : m_onSlayPlayer)
			if (cb(targetSlot, adminSlot)) return true;
		return false;
	}

	bool FireOnSilencePlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason)
	{
		for (auto &cb : m_onSilencePlayer)
			if (cb(targetSlot, adminSlot, timeMinutes, reason)) return true;
		return false;
	}

	bool FireOnMapChange(const char *mapName, int adminSlot)
	{
		for (auto &cb : m_onMapChange)
			if (cb(mapName, adminSlot)) return true;
		return false;
	}

	void FireOnUnbanPlayer(const char *authid, int adminSlot)
	{
		for (auto &cb : m_onUnbanPlayer) cb(authid, adminSlot);
	}

	void FireOnUnmutePlayer(int targetSlot, int adminSlot)
	{
		for (auto &cb : m_onUnmutePlayer) cb(targetSlot, adminSlot);
	}

	void FireOnUngagPlayer(int targetSlot, int adminSlot)
	{
		for (auto &cb : m_onUngagPlayer) cb(targetSlot, adminSlot);
	}

	void FireOnUnsilencePlayer(int targetSlot, int adminSlot)
	{
		for (auto &cb : m_onUnsilencePlayer) cb(targetSlot, adminSlot);
	}

	void FireOnReportPlayer(int reporterSlot, int targetSlot, const char *reason)
	{
		for (auto &cb : m_onReportPlayer) cb(reporterSlot, targetSlot, reason);
	}

	void FireOnClientPreAdminCheck(int slot)
	{
		for (auto &cb : m_onClientPreAdminCheck) cb(slot);
	}

	void FireOnClientConnected(int slot, const char *name, uint64_t steamid64, const char *ip)
	{
		for (auto &cb : m_onClientConnected) cb(slot, name, steamid64, ip);
	}

	void FireOnClientDisconnect(int slot)
	{
		for (auto &cb : m_onClientDisconnect) cb(slot);
	}

	void FireOnClientAuthorized(int slot, const char *authid, uint64_t steamid64)
	{
		for (auto &cb : m_onClientAuthorized) cb(slot, authid, steamid64);
	}

private:
	std::vector<OnBanPlayerFn> m_onBanPlayer;
	std::vector<OnUnbanPlayerFn> m_onUnbanPlayer;
	std::vector<OnMutePlayerFn> m_onMutePlayer;
	std::vector<OnGagPlayerFn> m_onGagPlayer;
	std::vector<OnUnmutePlayerFn> m_onUnmutePlayer;
	std::vector<OnUngagPlayerFn> m_onUngagPlayer;
	std::vector<OnReportPlayerFn> m_onReportPlayer;
	std::vector<OnClientPreAdminCheckFn> m_onClientPreAdminCheck;
	std::vector<OnKickPlayerFn> m_onKickPlayer;
	std::vector<OnSlayPlayerFn> m_onSlayPlayer;
	std::vector<OnSilencePlayerFn> m_onSilencePlayer;
	std::vector<OnUnsilencePlayerFn> m_onUnsilencePlayer;
	std::vector<OnMapChangeFn> m_onMapChange;
	std::vector<OnClientConnectedFn> m_onClientConnected;
	std::vector<OnClientDisconnectFn> m_onClientDisconnect;
	std::vector<OnClientAuthorizedFn> m_onClientAuthorized;
};

extern CS2AForwards g_CS2AForwards;

#endif // _INCLUDE_ADMIN_FORWARDS_H_
