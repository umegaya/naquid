//TODO(iyatomi): its really sensitive so use original ProofSourceChromium as it is. 
//
#pragma once

#include <string>

#include "net/quic/chromium/crypto/proof_source_chromium.h"

#include "naquid.h"

namespace net {

class NaquidProofSource : public ProofSourceChromium {
 public:
  NaquidProofSource(const nq_addr_t &addr) : ProofSourceChromium() {
    Initialize(addr);
  }
  ~NaquidProofSource() override {}

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
      //TODO(iyatomi): for linux, try to use /etc/letsencrypt/live/${domain_name}/privkey.pem, fullchain.pem
      inited = Initialize(
        base::FilePath(base::FilePath::StringPieceType("/etc/letsencrypt/live/") + a.host + "/fullchain.pem"), 
        base::FilePath(base::FilePath::StringPieceType("/etc/letsencrypt/live/") + a.host + "/privkey.pem"), 
        base::FilePath());
    }
#endif
    return inited;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(NaquidProofSource);
};

}  // namespace net