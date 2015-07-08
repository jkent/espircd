#include <esp8266.h>
#include <stdarg.h>
#include <stddef.h>

int ets_vsnprintf(char *str, size_t size, const char *format, va_list ap);

int ICACHE_FLASH_ATTR
snprintf(char *str, size_t str_m, const char *fmt, ...)
{
	va_list ap;
	int str_l;

	va_start(ap, fmt);
	str_l = ets_vsnprintf(str, str_m, fmt, ap);
	va_end(ap);
	return str_l;
}
