#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    int sleepMs = 200;
    int exitCode = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--sleep" && i + 1 < argc) {
            sleepMs = std::atoi(argv[++i]);
        } else if (argument == "--exit" && i + 1 < argc) {
            exitCode = std::atoi(argv[++i]);
        }
    }

    std::printf("ready\n");
    std::fflush(stdout);
    std::fprintf(stderr, "probe-started\n");
    std::fflush(stderr);

    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    return exitCode;
}
