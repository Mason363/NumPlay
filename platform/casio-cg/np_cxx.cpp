// C++ on the fx-CG for games with C++ files, built without exceptions: the C++ library's error paths stop the add-in
// instead of throwing, so its exception and wide-character I/O machinery stays out of the add-in
#include <stdlib.h>
namespace std {
[[noreturn]] void __throw_bad_alloc() { abort(); }
[[noreturn]] void __throw_bad_array_new_length() { abort(); }
[[noreturn]] void __throw_bad_function_call() { abort(); }
[[noreturn]] void __throw_length_error(const char *) { abort(); }
[[noreturn]] void __throw_logic_error(const char *) { abort(); }
[[noreturn]] void __throw_out_of_range(const char *) { abort(); }
[[noreturn]] void __throw_out_of_range_fmt(const char *, ...) { abort(); }
[[noreturn]] void __throw_invalid_argument(const char *) { abort(); }
[[noreturn]] void __throw_domain_error(const char *) { abort(); }
[[noreturn]] void __throw_runtime_error(const char *) { abort(); }
[[noreturn]] void __throw_overflow_error(const char *) { abort(); }
[[noreturn]] void __throw_underflow_error(const char *) { abort(); }
[[noreturn]] void __throw_range_error(const char *) { abort(); }
}  // namespace std
