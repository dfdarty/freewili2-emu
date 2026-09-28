// cpp_check: a WiliBSP app in C++. WiliBSP's headers have no extern "C"
// guards of their own (on the board as well), so a C++ app wraps them.
extern "C" {
#include "fw2.h"
#include "platform/diag.h"
}
#include "pico/stdlib.h"
#include <algorithm>
#include <string>
#include <vector>

struct Greeter {
    std::string name;
    explicit Greeter(const char *n) : name(n) {}
};
static Greeter s_static("static constructors ran");   // run before main()

int main(void) {
    board_init();
    fw2_app_recovery_init();
    std::vector<int> v{3, 1, 2};
    std::sort(v.begin(), v.end());
    std::string s = "sorted " + std::to_string(v[0]) + std::to_string(v[1]) + std::to_string(v[2]);
    DIAG("cpp_check: %s, %s\n", s.c_str(), s_static.name.c_str());
    for (;;) { fw2_app_recovery_task(); sleep_ms(10); }
}
