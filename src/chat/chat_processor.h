#ifndef _INCLUDE_ADMIN_CHAT_PROCESSOR_H_
#define _INCLUDE_ADMIN_CHAT_PROCESSOR_H_

#include "src/common.h"

#include <irecipientfilter.h>

#include <string>

class INetworkMessageInternal;
class CNetMessage;

// Renders player chat ourselves so tags can be injected into the line.
//
// Only one plugin on a server can own chat.
// We let say/say_team run and swap the game's SayText2 for our composed line as it is posted.
// Any plugin that supersedes say (a silent command, a menu keypress) stops the game from posting that line, so nothing gets rendered for it.
// That can't be learned from the say hook itself, since KHook runs every pre-hook and a void hook never sees another plugin's action.
// A second plugin doing the same produces two lines per message, so ownership yields whenever CS2AForeignPlugins reports another chat owner.
class CS2AChatProcessor
{
public:
	// True when we should render this player's chat instead of the game.
	bool ShouldRender(int slot) const;

	// Say dispatch pre-hook, once nothing of ours blocks the line.
	void BeginSay(int slot, const char *message, bool teamOnly);

	// Say dispatch post-hook. Runs whether or not anyone superseded.
	void EndSay(int slot);

	// The speaker slot when this posted event is the game's chat line for a pending say, otherwise -1.
	int MatchGameLine(INetworkMessageInternal *event, const CNetMessage *data) const;

	// Send our line for the pending say to the recipients the game picked for its own.
	// The game may post one event per recipient, so this can run several times for one say.
	void RenderPending(int slot, const CPlayerBitVec &recipients);

private:
	struct PendingSay
	{
		bool active = false;
		bool teamOnly = false;
		bool rendered = false;
		std::string message;
		std::string strippedLine;
	};

	std::string ComposeLine(int slot, const char *message, bool teamOnly) const;

	PendingSay m_pending[MAXPLAYERS + 1];
};

extern CS2AChatProcessor g_CS2AChatProcessor;

#endif // _INCLUDE_ADMIN_CHAT_PROCESSOR_H_
