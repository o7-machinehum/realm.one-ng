#include "game_server.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void printUsage() {
    std::printf("server [WORLD_PATH] [PORT]\n"
                "  defaults: data/world.dat 7000\n");
}

gs::ServerOptions parseArgs(int argc, char** argv) {
    gs::ServerOptions opts;
    if (argc >= 2) opts.world_path = argv[1];
    if (argc >= 3) opts.port = static_cast<uint16_t>(std::stoi(argv[2]));
    return opts;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--help") { printUsage(); return 0; }

    gs::GameServer server(parseArgs(argc, argv));
    if (!server.start()) return 1;
    server.runForever();
    return 0;
}
