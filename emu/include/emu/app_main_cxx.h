/* Forced into every C++ file of an app (fw2_emu_app.cmake). The app's
 * main() is renamed to fw2_emu_app_main() at compile time; declaring it
 * extern "C" first gives the renamed definition C linkage, so core.c's
 * call links whether the app is written in C or C++. */
#pragma once
extern "C" int fw2_emu_app_main(void);
