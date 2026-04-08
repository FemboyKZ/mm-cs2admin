#ifndef _INCLUDE_ADMIN_FORWARDS_H_
#define _INCLUDE_ADMIN_FORWARDS_H_

#include <functional>
#include <vector>
#include <string>

// Other Metamod plugins can acquire this interface via:
//   ICS2AdminForwards *fwd = (ICS2AdminForwards *)g_SMAPI->MetaFactory(
//       CS2ADMIN_FORWARDS_INTERFACE, nullptr, nullptr);
#define CS2ADMIN_FORWARDS_INTERFACE "ICS2AdminForwards001"

// Forward callback types.
// Returning true from On*Player callbacks blocks the action.
using OnBanPlayerFn = std::function<bool(int targetSlot, int adminSlot, int timeMinutes, const char *reason)>;
using OnUnbanPlayerFn = std::function<void(const char *authid, int adminSlot)>;
using OnMutePlayerFn = std::function<bool(int targetSlot, int adminSlot, int timeMinutes, const char *reason)>;
using OnGagPlayerFn = std::function<bool(int targetSlot, int adminSlot, int timeMinutes, const char *reason)>;
using OnUnmutePlayerFn = std::function<void(int targetSlot, int adminSlot)>;
using OnUngagPlayerFn = std::function<void(int targetSlot, int adminSlot)>;
using OnReportPlayerFn = std::function<void(int reporterSlot, int targetSlot, const char *reason)>;
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

	// Fire forwards, returns true if any callback blocked the action
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

	void FireOnReportPlayer(int reporterSlot, int targetSlot, const char *reason)
	{
		for (auto &cb : m_onReportPlayer) cb(reporterSlot, targetSlot, reason);
	}

	void FireOnClientPreAdminCheck(int slot)
	{
		for (auto &cb : m_onClientPreAdminCheck) cb(slot);
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
};

extern CS2AForwards g_CS2AForwards;

#endif // _INCLUDE_ADMIN_FORWARDS_H_
