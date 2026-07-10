#include "translations.h"

mmu::Translations g_CS2ATranslations;

std::string ADMIN_Translate(int slot, const char *phrase)
{
	return g_CS2ATranslations.Translate(ADMIN_SlotLanguage(slot), phrase ? phrase : "");
}
