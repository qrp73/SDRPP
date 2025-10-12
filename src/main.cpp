#include <core.h>
#include <stdio.h>
#include <utils/flog.h>

int main(int argc, char* argv[]) {
    try {
        return sdrpp_main(argc, argv);
    } catch (const std::exception& e) {
        flog::exception(e);
    } catch (...) {
        flog::exception();
    }
}