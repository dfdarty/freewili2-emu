/* app_flags.c — facts about the app that its CMake function records.
 *
 * Weak defaults, overridden by a generated file in the app's own target
 * (cmake/fw2_emu_app.cmake). They live apart from the code that reads them,
 * so the compiler can't fold the default into those reads. */
__attribute__((weak)) const int fw2_emu_psram_app = 0;   /* 1: built with fw2_psram_app() */
