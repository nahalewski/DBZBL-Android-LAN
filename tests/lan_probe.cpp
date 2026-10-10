#include <rex/net/lan.h>
#include <rex/net/online.h>
#include <rex/logging.h>
#include <rex/cvar.h>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  rex::InitLogging({}, spdlog::level::err);
  rex::net::online::SetGameVersion("lan-integration-test");
  if (argc < 2) return 2;
  const std::string mode = argv[1];
  const auto original_storage = rex::net::online::StorageProfileName();
  if (!rex::cvar::SetFlagByName("online_gamertag", "Mac M1") ||
      rex::net::online::PlayerName() != "Mac M1" ||
      rex::net::online::StorageProfileName() != original_storage) return 6;
  if (mode == "host") {
    rex::net::online::SetPlayerName("MacHost");
    rex::net::lan::SetContext(3, 1);
    rex::net::lan::SetContext(4, 60);
    rex::net::lan::SetProperty(0x1000001E, 7);
    uint8_t info[0x3C] = {0xAE, 127, 0, 0, 2, 0x52, 0x58, 1};
    rex::net::lan::HostCreate(info, 2, 0);
    std::cout << "READY" << std::endl;
    std::string command;
    while (std::getline(std::cin, command)) {
      if (command == "rename") {
        const auto storage = rex::net::online::StorageProfileName();
        if (!rex::net::online::SetPlayerName("NewTag") ||
            rex::net::online::StorageProfileName() != storage ||
            rex::net::online::SetPlayerName(std::string(16, 'X')) ||
            rex::net::online::SetPlayerName("bad\nname")) return 3;
      } else if (command == "full") rex::net::lan::RemotePlayers(1, true);
      else if (command == "open") rex::net::lan::RemotePlayers(1, false);
      else if (command == "delete") rex::net::lan::SessionDelete();
      else if (command == "private") rex::net::lan::HostCreate(info, 0, 2);
      else if (command == "quit") break;
      std::cout << "OK" << std::endl;
    }
  } else if (mode == "search") {
    const auto rooms = rex::net::lan::Search(5, {{3, 1}});
    for (const auto& room : rooms)
      std::cout << "ROOM " << room.host_name << " " << room.host_ip << " "
                << room.contexts.size() << " " << room.properties.size() << std::endl;
    std::cout << "COUNT " << rooms.size() << std::endl;
  } else if (mode == "empty") {
    if (!rex::net::lan::Search(5, {{3, 2}}).empty()) return 4;
    if (!rex::net::lan::Search(0, {}).empty()) return 5;
  } else return 2;
  return 0;
}
