#pragma once

#include "net/tools/quic/quic_client_base.h"

#include "core/compat/chromium/nq_client_session.h"
#include "core/compat/chromium/nq_network_helper.h"
#include "core/compat/chromium/nq_stub_interface.h"
#include "core/compat/chromium/nq_packet_writer.h"

namespace net {
class NqClientCompat;
class NqClientLoop;
class NqQuicClient : public QuicClientBase,
										public QuicCryptoClientStream::ProofHandler {
 public:
	NqQuicClient(NqClientCompat *client, 
							NqClientLoop &loop,
							const NqQuicServerId &server_id,
							const NqClientConfig &config);
	~NqQuicClient() override {}

	// operation
	void ForceShutdown();

	// getter/setter
	inline NqClientSession *nq_session() { return static_cast<NqClientSession *>(session()); }
	inline const NqClientSession *nq_session() const { return const_cast<NqQuicClient *>(this)->nq_session(); }
	inline NqPacketWriter *packet_writer() { return static_cast<NqPacketWriter *>(writer()); }
	inline NqQuicConnectionId connection_id() { return session()->connection_id(); }
	NqClientLoop *loop();

	// implements QuicClientBase. TODO(umegaya): these are really not needed? (GetNum*)
	int GetNumSentClientHellosFromSession() override { return 0; }
	int GetNumReceivedServerConfigUpdatesFromSession() override { return 0; }
	void ResendSavedData() override {}
	void ClearDataToResend() override {}
	std::unique_ptr<QuicSession> CreateQuicClientSession(QuicConnection* connection) override;
	void InitializeSession() override;

	// implements QuicCryptoClientStream::ProofHandler
	// Called when the proof in |cached| is marked valid.  If this is a secure
	// QUIC session, then this will happen only after the proof verifier
	// completes.
	void OnProofValid(const QuicCryptoClientConfig::CachedState& cached) override;
	// Called when proof verification details become available, either because
	// proof verification is complete, or when cached details are used. This
	// will only be called for secure QUIC connections.
	void OnProofVerifyDetailsAvailable(const ProofVerifyDetails& verify_details) override;

 private:
	NqClientCompat *client_;
};
} // net