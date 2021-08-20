#include "core/nq_client.h"
#include "core/nq_client_loop.h"

namespace nq {
void* NqClient::operator new(std::size_t sz) {
  ASSERT(false);
  auto r = reinterpret_cast<NqClient *>(std::malloc(sz));
  r->loop_ = nullptr;
  return r;
}
void* NqClient::operator new(std::size_t sz, NqClientLoop *l) {
  auto r = reinterpret_cast<NqClient *>(l->client_allocator().Alloc(sz));
  r->loop_ = l;
  return r;
}
void NqClient::operator delete(void *p) noexcept {
  auto r = reinterpret_cast<NqClient *>(p);
  if (r->loop_ == nullptr) {
    std::free(r);
  } else {
    r->loop_->client_allocator().Free(r);
  }
}
void NqClient::operator delete(void *p, NqClientLoop *l) noexcept {
  l->client_allocator().Free(p);
}
}
