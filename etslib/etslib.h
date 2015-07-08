#ifndef ETSLIB_H
#define ETSLIB_H

#include "esp_missing.h"

int ICACHE_FLASH_ATTR snprintf(char *str, size_t size, const char *format, ...) __attribute__ ((format (printf, 3, 4)));
void ICACHE_FLASH_ATTR stdout_init(void);
int ICACHE_FLASH_ATTR strcasecmp(const char *s1, const char *s2);
int ICACHE_FLASH_ATTR strncasecmp(const char *s1, const char *s2, size_t n);
char *ICACHE_FLASH_ATTR strdup(const char *s);

#define bzero ets_bzero
#define memcmp ets_memcmp
#define memcpy ets_memcpy
#define memset ets_memset
#define strchr ets_strchr
#define strrchr ets_strrchr
#define strcmp ets_strcmp
#define strncmp ets_strncmp
#define strcpy ets_strcpy
#define strncpy ets_strncpy
#define strlen ets_strlen
#define strstr ets_strstr
#define malloc pvPortMalloc
#define zalloc pvPortZalloc
#define calloc pvPortCalloc
#define realloc pvPortRealloc
#define free vPortFree
#define vsnprintf ets_vsnprintf
#define sprintf ets_sprintf
#define printf(format, ...) \
	ets_printf(format, ##__VA_ARGS__)
#define usleep ets_delay_us

#endif /* ETSLIB_H */
