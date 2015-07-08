/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

#include <esp8266.h>
#include "ircd.h"

//#define SHOW_HEAP_USE

#ifdef SHOW_HEAP_USE
static ETSTimer prHeapTimer;

static void ICACHE_FLASH_ATTR
prHeapTimerCb(void *arg)
{
	printf("Heap: %ld\n", (unsigned long)system_get_free_heap_size());
}
#endif

void ICACHE_FLASH_ATTR
user_init(void)
{
	stdout_init();
	printf("\n");

	ircdInit(6667);
#ifdef SHOW_HEAP_USE
	os_timer_disarm(&prHeapTimer);
	os_timer_setfn(&prHeapTimer, prHeapTimerCb, NULL);
	os_timer_arm(&prHeapTimer, 3000, 1);
#endif
	printf("\nReady\n");
}

#ifndef USE_OPENSDK
void ICACHE_FLASH_ATTR
user_rf_pre_init(void)
{
}
#endif
