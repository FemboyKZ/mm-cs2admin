#ifndef _INCLUDE_CS2A_TRANSLATIONS_H_
#define _INCLUDE_CS2A_TRANSLATIONS_H_

#include "utils/translations.h"

#include <string>

// SourceMod-style phrase tables for CS2Admin's chat and command text, resolved per client language.
extern mmu::Translations g_CS2ATranslations;

// Defined in cs2admin.cpp.
// Returns the mapped phrase-file language key for the client in `slot`, or the default language when unavailable.
std::string CS2A_SlotLanguage(int slot);

// Defined in cs2admin.cpp.
// Reloads phrase tables. Call after (re)loading the plugin config.
void CS2A_LoadTranslations();

// Translate `phrase` into the language of the client in `slot`.
// Convenience wrapper over g_CS2ATranslations for building menu titles and labels.
std::string CS2A_Translate(int slot, const char *phrase);

// CS2A_Translate with the phrase's format specifiers filled in.
std::string CS2A_Format(int slot, const char *phrase, ...);

#endif // _INCLUDE_CS2A_TRANSLATIONS_H_
