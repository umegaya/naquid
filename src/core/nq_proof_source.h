//TODO(iyatomi): its really sensitive so use original ProofSourceChromium as it is. 
//
#pragma once

#include <string>

#include "net/quic/chromium/crypto/proof_source_chromium.h"

#include "nq.h"

namespace net {

class NqProofSource : public ProofSourceChromium {
 public:
  NqProofSource(const nq_addr_t &addr) : ProofSourceChromium() {
    Initialize(addr);
  }
  ~NqProofSource() override {}

  bool Initialize(const nq_addr_t &a) {
    bool inited = false;
    if (a.cert != nullptr && a.key != nullptr) {
      inited = ProofSourceChromium::Initialize(
        base::FilePath(base::FilePath::StringPieceType(a.cert)), 
        base::FilePath(base::FilePath::StringPieceType(a.key)), 
        base::FilePath());

    } 
#if defined(OS_LINUX)
    if (!inited) {
      auto basepath = base::FilePath::StringType("/etc/letsencrypt/live/") + 
                      base::FilePath::StringType(a.host), 
           certfile = base::FilePath::StringType("/fullchain.pem"),
           keyfile = base::FilePath::StringType("/privkey.pem");
      //TODO(iyatomi): for linux, try to use /etc/letsencrypt/live/${domain_name}/privkey.pem, fullchain.pem
      inited = ProofSourceChromium::Initialize(
        base::FilePath(basepath + certfile), 
        base::FilePath(basepath + keyfile), 
        base::FilePath());
    }
#endif
    return inited;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(NqProofSource);
};

}  // namespace net