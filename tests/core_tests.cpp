#include "core.h"
#include <iostream>
#include <limits>
#include <stdexcept>
void require(bool value, const char *name) {
    if (!value)
        throw std::runtime_error(name);
}
int main() {
    using namespace monitor;
    auto cpu = cpuUsage({100, 200, 100}, {150, 300, 200});
    require(cpu && std::abs(*cpu - 75) < 0.001, "kernel includes idle");
    require(!cpuUsage({0, 0, 0}, {0, 0, 0}), "zero elapsed");
    require(!cpuUsage({100, 200, 100}, {99, 300, 200}), "counter rollback");
    require(!cpuUsage({0, 0, 0}, {100, 10, 10}), "inconsistent idle");
    auto process = processUsage(0, 10000000, 1, 8);
    require(process && std::abs(*process - 12.5) < 0.001, "machine CPU normalization");
    require(!processUsage(10, 9, 1, 1), "process rollback");
    require(!processUsage(0, 10, 0, 1), "zero interval");
    require(!processUsage(0, 10, 1, 0), "zero processors");
    require(!processUsage(0, 10, std::numeric_limits<double>::quiet_NaN(), 1), "invalid clock");
    History<int> history(2);
    history.push(1);
    history.push(2);
    history.push(3);
    require(history.data().size() == 2 && history.data().front() == 2, "bounded history");
    History<int> empty(0);
    empty.push(1);
    require(empty.data().empty(), "zero history");
    std::cout << "11 core checks passed\n";
}
