// Linux desktop link (CMakeLists.txt): the game image sits above 2 GiB, so
// gcc's crtbegin.o, a small-code-model object, is left out.  It also defines
// __dso_handle, which every C++ static destructor registration refers to.
#if defined(__linux__) && !defined(__ANDROID__)
__attribute__((visibility("hidden"))) void* __dso_handle = &__dso_handle;
#endif
