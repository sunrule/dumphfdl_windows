/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * dll_entry.c - DllMain for the dumphfdl DLL.
 *
 * The DLL does not need any special initialization on load/unload:
 * all resources are allocated and freed inside HFDL_DecodeFile().
 */
#ifdef _WIN32
#include <windows.h>
#include "util.h"                       // UNUSED

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
	UNUSED(hinstDLL);
	UNUSED(lpvReserved);
	switch(fdwReason) {
		case DLL_PROCESS_ATTACH:
			// Disable thread attach/detach notifications for speed.
			DisableThreadLibraryCalls(hinstDLL);
			break;
		case DLL_PROCESS_DETACH:
			break;
		default:
			break;
	}
	return TRUE;
}

#endif
