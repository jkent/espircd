#include <esp8266.h>

char *ICACHE_FLASH_ATTR
strdup(const char *s)
{
	if (s == NULL) {
		return NULL;
	}
	char *new = (char *)malloc(strlen(s) + 1);
	if (new == NULL) {
		return NULL;
	}
	strcpy(new, s);
	return new;
}
