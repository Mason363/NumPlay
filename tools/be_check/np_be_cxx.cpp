// The C++ runtime the games with C++ files need, for tools/be_check.py (no exceptions, no RTTI)
#include <stdlib.h>
#include <new>
__attribute__((weak)) void *operator new(size_t n) { return malloc(n ? n : 1); }
__attribute__((weak)) void *operator new[](size_t n) { return malloc(n ? n : 1); }
__attribute__((weak)) void operator delete(void *p) noexcept { free(p); }
__attribute__((weak)) void operator delete[](void *p) noexcept { free(p); }
__attribute__((weak)) void operator delete(void *p, size_t) noexcept { free(p); }
__attribute__((weak)) void operator delete[](void *p, size_t) noexcept { free(p); }
extern "C" __attribute__((weak)) void __cxa_pure_virtual() { abort(); }
extern "C" __attribute__((weak)) int __aeabi_atexit(void *, void (*)(void *), void *) { return 0; }
__attribute__((weak)) void *__dso_handle;
namespace std {
__attribute__((weak)) [[noreturn]] void __throw_bad_alloc() { abort(); }
__attribute__((weak)) [[noreturn]] void __throw_bad_array_new_length() { abort(); }
__attribute__((weak)) [[noreturn]] void __throw_bad_function_call() { abort(); }
__attribute__((weak)) [[noreturn]] void __throw_length_error(const char *) { abort(); }
__attribute__((weak)) [[noreturn]] void __throw_logic_error(const char *) { abort(); }
__attribute__((weak)) [[noreturn]] void __throw_out_of_range(const char *) { abort(); }
__attribute__((weak)) [[noreturn]] void __throw_out_of_range_fmt(const char *, ...) { abort(); }
__attribute__((weak)) [[noreturn]] void __throw_invalid_argument(const char *) { abort(); }
__attribute__((weak)) [[noreturn]] void __throw_domain_error(const char *) { abort(); }
__attribute__((weak)) [[noreturn]] void __throw_runtime_error(const char *) { abort(); }
__attribute__((weak)) [[noreturn]] void __throw_overflow_error(const char *) { abort(); }
__attribute__((weak)) [[noreturn]] void __throw_underflow_error(const char *) { abort(); }
__attribute__((weak)) [[noreturn]] void __throw_range_error(const char *) { abort(); }
}  // namespace std
