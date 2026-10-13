#include "translations.h"

#include <cstdarg>
#include <cstdio>

mmu::Translations g_CS2ATranslations;

std::string CS2A_Translate(int slot, const char *phrase)
{
	return g_CS2ATranslations.Translate(CS2A_SlotLanguage(slot), phrase ? phrase : "");
}

std::string CS2A_Format(int slot, const char *phrase, ...)
{
	std::string tmpl = CS2A_Translate(slot, phrase);
	char buffer[512];
	va_list args;
	va_start(args, phrase);
	vsnprintf(buffer, sizeof(buffer), tmpl.c_str(), args);
	va_end(args);
	return buffer;
}
