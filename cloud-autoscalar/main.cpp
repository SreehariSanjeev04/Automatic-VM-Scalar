#include "Orchestrator.hpp"

int main(int argc, char **argv) {
    Orchestrator::run(argc > 1 ? argv[1] : "autoscaler.config");
}