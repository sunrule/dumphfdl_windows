/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * hfdl_dll_test.c - test harness for the dumphfdl DLL API.
 *
 * Loads dumphfdl.dll, calls HFDL_Version() and HFDL_DecodeFile()
 * on a test MCR file, counts the messages received via the callback.
 *
 * This replaces a Delphi 7 test project (Delphi is not available in
 * the build environment). It exercises exactly the same API surface
 * that the Delphi unit imports.
 *
 * Usage: hfdl_dll_test.exe <dll_path> <mcr_file> [systable_file]
 * Exit code: 0 = success, non-zero = failure
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Mirror of the API declared in src/hfdl_api.h
typedef int (__stdcall *hfdl_decode_fn)(
		char const *mcr_path, double bearing, double elevation,
		char const *systable_path,
		void (*callback)(char const *msg, void *userdata),
		void *userdata);
typedef char const * (__stdcall *hfdl_version_fn)(void);

static int msg_count = 0;
static int first_msg_shown = 0;

static void __stdcall msg_callback(char const *msg, void *userdata) {
	(void)userdata;
	msg_count++;
	if(!first_msg_shown && msg != NULL) {
		printf("FIRST MSG: %.200s\n", msg);
		first_msg_shown = 1;
	}
}

int main(int argc, char **argv) {
	if(argc < 3) {
		fprintf(stderr, "Usage: %s <dll_path> <mcr_file> [systable_file]\n", argv[0]);
		return 2;
	}
	char const *dll_path = argv[1];
	char const *mcr_path = argv[2];
	char const *systable_path = (argc > 3) ? argv[3] : NULL;

#ifdef _WIN32
	HMODULE h = LoadLibraryA(dll_path);
	if(h == NULL) {
		fprintf(stderr, "LoadLibrary('%s') failed: %lu\n", dll_path, GetLastError());
		return 1;
	}
	hfdl_version_fn version = (hfdl_version_fn)GetProcAddress(h, "HFDL_Version");
	hfdl_decode_fn decode = (hfdl_decode_fn)GetProcAddress(h, "HFDL_DecodeFile");
#else
	void *h = dlopen(dll_path, RTLD_NOW);
	if(h == NULL) {
		fprintf(stderr, "dlopen('%s') failed: %s\n", dll_path, dlerror());
		return 1;
	}
	hfdl_version_fn version = (hfdl_version_fn)dlsym(h, "HFDL_Version");
	hfdl_decode_fn decode = (hfdl_decode_fn)dlsym(h, "HFDL_DecodeFile");
#endif

	if(version == NULL || decode == NULL) {
		fprintf(stderr, "Missing exports (version=%p decode=%p)\n",
				(void *)version, (void *)decode);
		return 1;
	}

	printf("DLL version: %s\n", version());
	printf("Decoding: %s (bearing=45, elev=10, systable=%s)\n",
			mcr_path, systable_path ? systable_path : "(none)");

	int rc = decode(mcr_path, 45.0, 10.0, systable_path, msg_callback, NULL);
	printf("HFDL_DecodeFile returned: %d\n", rc);
	printf("Messages received: %d\n", msg_count);

#ifdef _WIN32
	FreeLibrary(h);
#else
	dlclose(h);
#endif

	if(rc != 0) {
		fprintf(stderr, "FAIL: decode returned error %d\n", rc);
		return 1;
	}
	if(msg_count == 0) {
		fprintf(stderr, "FAIL: no messages decoded\n");
		return 1;
	}
	printf("PASS\n");
	return 0;
}
