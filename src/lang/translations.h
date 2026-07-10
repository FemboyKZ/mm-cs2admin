#ifndef _INCLUDE_ADMIN_TRANSLATIONS_H_
#define _INCLUDE_ADMIN_TRANSLATIONS_H_

#include "mmu/translations.h"

#include <string>

// SourceMod-style phrase tables for CS2Admin's chat and command text, resolved per client language.
extern mmu::Translations g_CS2ATranslations;

// Defined in cs2admin.cpp.
// Returns the mapped phrase-file language key for the client in `slot`, or the default language when unavailable.
std::string ADMIN_SlotLanguage(int slot);

// Defined in cs2admin.cpp.
// Re-acquires ClientCvarValue and reloads phrase tables. Call after (re)loading the plugin config.
void ADMIN_LoadTranslations();

// Translate `phrase` into the language of the client in `slot`.
// Convenience wrapper over g_CS2ATranslations for building menu titles and labels.
std::string ADMIN_Translate(int slot, const char *phrase);

#endif // _INCLUDE_ADMIN_TRANSLATIONS_H_
