#include "interop/naquid_client_loop.h"

#include "interop/naquid_client.h"

namespace net {
void NaquidClientLoop::Close() {
  for (auto &kv : client_map_) {
    delete kv.second;
  }
  NaquidLoop::Close();
}
void NaquidClientLoop::RemoveClient(NaquidClient *cl) {
  auto it = client_map_.find(cl->server_address().ToString());
  if (it != client_map_.end()) {
    client_map_.erase(it);
  }
}
NaquidClient *NaquidClientLoop::Create(const std::string &url, 
                                       NaquidClientConfig &config) {
  QuicServerId server_id;
  QuicSocketAddress server_address;
  if (!ParseUrl(url, server_id, server_address, config)) {
    return nullptr;
  }
  auto c = new NaquidClient(
      server_address,
      server_id, 
      AllSupportedVersions(),
      config,
      this,
      config.proof_verifier()
  );
  client_map_[server_address.ToString()] = c;
  return c;
}
/* static */
bool NaquidClientLoop::ParseUrl(const std::string &url, QuicServerId& server_id, QuicSocketAddress &address, QuicConfig &config) {
  std::string host, port;
  auto pos = url.find("://");
  if (pos != std::string::npos) {
    auto hpos = url.find(":", pos);
    auto spos = url.find("?", pos);
    host = url.substr(pos, hpos - pos);
    port = url.substr(hpos, spos - hpos);
  }
  if (host.empty()) {
    return false;
  } else if (port.empty()) {
    port = "443";
  }
  //TODO: being asynchronous somehow. using chromium one or cares
  struct addrinfo filter, *resolved;
  filter.ai_socktype = SOCK_DGRAM;
  filter.ai_family = AF_UNSPEC;
  filter.ai_protocol = 0;
  filter.ai_flags = 0;
  if (getaddrinfo(host.c_str(), port.c_str(), &filter, &resolved) != 0) {
    return false;
  }
  try {
    size_t idx;
    int portnum = std::stoi(port, &idx);
    if (idx != port.length()) {
      freeaddrinfo(resolved);
      return false;
    }
    server_id = QuicServerId(host, portnum, PRIVACY_MODE_ENABLED); //TODO: control privacy mode from url
    address = QuicSocketAddress(*resolved->ai_addr);
    freeaddrinfo(resolved);
    return true;
  } catch (...) {
    freeaddrinfo(resolved);
    return false;
  }
}	
} //net