#pragma once
#include <rex/system/xsocket.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <android/log.h>
#include <string>

namespace burstlimit::android {
inline bool LanProbeFailure(int line) {
  __android_log_print(ANDROID_LOG_ERROR, "BurstLimitLAN", "Socket diagnostic failure at line %d: errno=%d (%s)", line, errno, strerror(errno));
  return false;
}
inline bool ProbeSocketOptions() {
  using rex::system::XSocket;
  XSocket socket(nullptr);
  if (socket.Initialize(XSocket::X_AF_INET, XSocket::X_SOCK_DGRAM, XSocket::X_IPPROTO_VDP) != 0)
    return LanProbeFailure(__LINE__);
  rex::be<int32_t> value = 1;
  if (socket.SetOption(0xFFFF, 0x0020, &value, 4) != 0) return LanProbeFailure(__LINE__);
  int actual = 0;
  socklen_t size = sizeof(actual);
  if (getsockopt(socket.native_handle(), SOL_SOCKET, SO_BROADCAST, &actual, &size) != 0 || actual != 1)
    return LanProbeFailure(__LINE__);
  value = 131072;
  if (socket.SetOption(0xFFFF, 0x1002, &value, 4) != 0) return LanProbeFailure(__LINE__);
  if (getsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVBUF, &actual, &size) != 0 || actual < 131072)
    return LanProbeFailure(__LINE__);
  value = 250;
  timeval timeout{};
  size = sizeof(timeout);
  if (socket.SetOption(0xFFFF, 0x1006, &value, 4) != 0 ||
      getsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &timeout, &size) != 0 ||
      timeout.tv_sec != 0 || timeout.tv_usec < 250000 || timeout.tv_usec > 300000)
    return LanProbeFailure(__LINE__);
  value = 1;
  if (socket.IOControl(0x8004667E, reinterpret_cast<uint8_t*>(&value)) != 0) return LanProbeFailure(__LINE__);
  rex::system::N_XSOCKADDR_IN address{};
  address.sin_family = 2;
  address.sin_addr = 0x7F000001;
  if (socket.Bind(&address, 16) != 0) return LanProbeFailure(__LINE__);
  uint8_t packet[16]{};
  rex::system::N_XSOCKADDR_IN source{};
  source.sin_addr = 0x12345678;
  uint32_t source_size = 16;
  if (socket.RecvFrom(packet, sizeof(packet), 0, &source, &source_size) != -1 ||
      errno != EAGAIN || uint32_t(source.sin_addr) != 0x12345678)
    return LanProbeFailure(__LINE__);
  // Invalid requests must fail rather than silently applying the wrong option.
  if (socket.SetOption(0xFFFF, 0x7777, &value, 4) == 0 || errno != ENOPROTOOPT) return LanProbeFailure(__LINE__);
  if (socket.IOControl(0x7777, reinterpret_cast<uint8_t*>(&value)) == 0 || errno != EINVAL) return LanProbeFailure(__LINE__);
  sockaddr_in native{};
  size = sizeof(native);
  if (getsockname(socket.native_handle(), reinterpret_cast<sockaddr*>(&native), &size) != 0) return LanProbeFailure(__LINE__);
  address.sin_port = native.sin_port;
  if (socket.SendTo(packet, sizeof(packet), 0, &address, 16) != sizeof(packet)) return LanProbeFailure(__LINE__);
  value = 0;
  if (socket.IOControl(0x4004667F, reinterpret_cast<uint8_t*>(&value)) != 0 || int32_t(value) != sizeof(packet))
    return LanProbeFailure(__LINE__);
  return socket.RecvFrom(packet, sizeof(packet), 0, &source, &source_size) == sizeof(packet) &&
         uint32_t(source.sin_addr) == 0x7F000001 && uint16_t(source.sin_port) == native.sin_port;
}

inline bool ProbeUdpEcho(const std::string& peer, std::string& details) {
  using rex::system::XSocket;
  XSocket socket(nullptr);
  if (socket.Initialize(XSocket::X_AF_INET, XSocket::X_SOCK_DGRAM, XSocket::X_IPPROTO_VDP) != 0)
    return LanProbeFailure(__LINE__);
  rex::be<int32_t> nonblocking = 1;
  if (socket.IOControl(0x8004667E, reinterpret_cast<uint8_t*>(&nonblocking)) != 0) return LanProbeFailure(__LINE__);
  rex::system::N_XSOCKADDR_IN local{};
  local.sin_family = 2;
  // Use the game's actual network port (59395) for this diagnostic.
  local.sin_port = 1000;
  if (socket.Bind(&local, 16) != 0) return LanProbeFailure(__LINE__);
  in_addr ip{};
  if (inet_pton(AF_INET, peer.c_str(), &ip) != 1) return LanProbeFailure(__LINE__);
  rex::system::N_XSOCKADDR_IN remote{};
  remote.sin_family = 2;
  remote.sin_addr = ntohl(ip.s_addr);
  remote.sin_port = htons(61198);
  const auto started = std::chrono::steady_clock::now();
  int received = 0, retransmits = 0;
  for (int sequence = 0; sequence < 32; ++sequence) {
    std::array<uint8_t, 320> packet{};
    std::memcpy(packet.data(), "BurstLimitLANProbe", 18);
    packet[18] = sequence;
    for (size_t i = 19; i < packet.size(); ++i) packet[i] = uint8_t(i + sequence);
    bool matched = false;
    for (int attempt = 0; attempt < 3 && !matched; ++attempt) {
      if (std::chrono::steady_clock::now() - started > std::chrono::seconds(8)) break;
      if (attempt) ++retransmits;
      if (socket.SendTo(packet.data(), packet.size(), 0, &remote, 16) != packet.size()) break;
      fd_set readable;
      FD_ZERO(&readable);
      FD_SET(socket.native_handle(), &readable);
      timeval wait{0, 250000};
      if (select(socket.native_handle() + 1, &readable, nullptr, nullptr, &wait) != 1) continue;
      std::array<uint8_t, 321> echo{};
      rex::system::N_XSOCKADDR_IN from{};
      uint32_t from_size = 16;
      int size = socket.RecvFrom(echo.data(), echo.size(), 0, &from, &from_size);
      matched = size == packet.size() && uint32_t(from.sin_addr) == uint32_t(remote.sin_addr) &&
                uint16_t(from.sin_port) == uint16_t(remote.sin_port) &&
                std::memcmp(echo.data(), packet.data(), packet.size()) == 0;
    }
    if (!matched) break;
    ++received;
  }
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  details = "UDP peer: " + peer + ":61198; local port 59395; " + std::to_string(received) +
            "/32 verified 320-byte round trips; retransmits=" + std::to_string(retransmits) +
            "; elapsed=" + std::to_string(elapsed) + "ms\n";
  return received == 32;
}
}  // namespace burstlimit::android
