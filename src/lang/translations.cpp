#include "translations.h"

#include <cstdarg>
#include <cstdio>

mmu::Translations g_CS2ATranslations;

std::string ADMIN_Translate(int slot, const char *phrase)
{
	return g_CS2ATranslations.Translate(ADMIN_SlotLanguage(slot), phrase ? phrase : "");
}

std::string ADMIN_Format(int slot, const char *phrase, ...)
{
	std::string tmpl = ADMIN_Translate(slot, phrase);
	char buffer[512];
	va_list args;
	va_start(args, phrase);
	vsnprintf(buffer, sizeof(buffer), tmpl.c_str(), args);
	va_end(args);
	return buffer;
}
