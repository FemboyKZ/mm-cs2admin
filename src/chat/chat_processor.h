#ifndef _INCLUDE_ADMIN_CHAT_PROCESSOR_H_
#define _INCLUDE_ADMIN_CHAT_PROCESSOR_H_

#include "src/common.h"

#include <string>

// Renders player chat ourselves so tags can be injected into the line.
//
// Only one plugin on a server can own chat.
// We take it by superseding say/say_team so the game never runs Host_Say and never emits its own line, then post our own composed SayText2.
// A second plugin doing the same produces two lines per message, so ownership yields whenever CS2AForeignPlugins reports another chat owner.
class CS2AChatProcessor
{
public:
	// True when we should render this player's chat instead of the game.
	bool ShouldRender(int slot) const;

	// Compose and send one player chat line. Only call when ShouldRender passed.
	void RenderPlayerChat(int slot, const char *message, bool teamOnly);

private:
	std::string ComposeLine(int slot, const char *message, bool teamOnly) const;
	// Whether `listener` should receive `speaker`'s message.
	// Applies the team-only rule the game would have applied had we not superseded say.
	bool CanHear(int listener, int speaker, bool teamOnly) const;
};

extern CS2AChatProcessor g_CS2AChatProcessor;

#endif // _INCLUDE_ADMIN_CHAT_PROCESSOR_H_
