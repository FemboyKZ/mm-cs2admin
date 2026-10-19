#include "translations.h"

#include "game/cvarquery.h"

#include <cstdarg>
#include <cstdio>

mmu::Translations g_CS2ATranslations;

std::string CS2A_Translate(int slot, const char *phrase)
{
	return mmu::cvarquery::Translate(g_CS2ATranslations, slot, phrase);
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
