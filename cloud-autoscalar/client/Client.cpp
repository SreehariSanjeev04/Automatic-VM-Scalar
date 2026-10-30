#include "LoadGenerator.hpp"
#include "Client.hpp"
#include "httplib.h"
#include "Constant.hpp"

Client::Client(int mode) : mode(mode) {}

void Client::generateTraffic() {
    if (mode == Mode::low) {
        LoadGenerator::generateLowTraffic();
    } else {
        LoadGenerator::generateHighTraffic();
    }
}