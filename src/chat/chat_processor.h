#ifndef _INCLUDE_ADMIN_CHAT_PROCESSOR_H_
#define _INCLUDE_ADMIN_CHAT_PROCESSOR_H_

#include "src/common.h"

#include <irecipientfilter.h>

#include <string>
#include <vector>

class INetworkMessageInternal;
class CNetMessage;

// Renders player chat ourselves so tags can be injected into the line.
//
// Only one plugin on a server can own chat.
// We let say/say_team run and swap the game's chat line (SayText2, or SayText) for our composed line as it is posted.
// Any plugin that supersedes say (a silent command, a menu keypress) stops the game from posting that line, so nothing gets rendered for it.
// That can't be learned from the say hook itself, since KHook runs every pre-hook and a void hook never sees another plugin's action.
// cs2kz's overridePlayerChat blocks the game's line the same way, see its kz_quiet.cpp.
// A second plugin doing the same produces two lines per message, so ownership yields whenever CS2AForeignPlugins reports another chat owner.
class CS2AChatProcessor
{
public:
	// True when we should render this player's chat instead of the game.
	bool ShouldRender(int slot) const;

	// Say dispatch pre-hook, first thing for every say we track, including ones we then supersede.
	// A plugin can dispatch another say from inside one, so says stack and each post-hook pops its own.
	void PushSay(int slot);

	// Mark the innermost say for rendering, once nothing of ours blocks the line.
	void RenderSay(const char *message, bool teamOnly);

	// Say dispatch post-hook, for exactly the says PushSay saw. Runs whether or not anyone superseded.
	void PopSay();

	// The speaker slot when this posted event is the game's chat line for the innermost say, otherwise -1.
	// Say runs synchronously, so the game posts a say's line while that say is on top.
	int MatchGameLine(INetworkMessageInternal *event, const CNetMessage *data) const;

	// Send our line to the recipients the game picked for its own.
	// The game may post several events for one say, so recipients already sent to are skipped.
	void RenderPending(int slot, const CPlayerBitVec &recipients);

private:
	struct PendingSay
	{
		int slot = -1;
		bool render = false;
		bool teamOnly = false;
		std::string message;
		CPlayerBitVec sentTo;
	};

	std::string ComposeLine(int slot, const char *message, bool teamOnly) const;

	std::vector<PendingSay> m_says;
};

extern CS2AChatProcessor g_CS2AChatProcessor;

#endif // _INCLUDE_ADMIN_CHAT_PROCESSOR_H_
