#include "game_client.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void printUsage() {
    std::printf("client [--host HOST] [--port PORT] [--name NAME] [--world PATH]\n");
}

gc::ClientOptions parseArgs(int argc, char** argv) {
    gc::ClientOptions opts;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(1); }
            return argv[++i];
        };
        if      (a == "--host")  opts.host = next();
        else if (a == "--port")  opts.port = static_cast<uint16_t>(std::stoi(next()));
        else if (a == "--name")  opts.player_name = next();
        else if (a == "--world") opts.world_path = next();
        else if (a == "--help" || a == "-h") { printUsage(); std::exit(0); }
    }
    return opts;
}

} // namespace

int main(int argc, char** argv) {
    gc::GameClient client(parseArgs(argc, argv));
    if (!client.init()) return 1;
    client.runUntilClosed();
    return 0;
}
