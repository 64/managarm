#pragma once

#include <arch/dma_pool.hpp>
#include <async/queue.hpp>
#include <async/recurring-event.hpp>
#include <helix/ipc.hpp>
#include <netserver/nic.hpp>
#include <protocols/fs/server.hpp>
#include <vector>

struct RawSocket;

struct Raw {
	managarm::fs::Errors serveSocket(helix::UniqueLane lane, int type, int proto, int flags);
	void feedPacket(arch::dma_buffer_view frame);

  private:
	friend RawSocket;

	std::vector<smarter::shared_ptr<RawSocket>> sockets_;
	std::vector<smarter::shared_ptr<RawSocket>> binds_;
};

struct RawSocket {
	explicit RawSocket(Raw *parent, int proto) : parent{parent}, proto(proto) {}

	static async::result<protocols::fs::Error>
	bind(void *obj, const char *creds, const void *addr_ptr, size_t addr_size);

	static async::result<frg::expected<protocols::fs::Error, size_t>>
	write(void *object, const char *credentials, const void *buffer, size_t length);

	static async::result<protocols::fs::RecvResult> recvmsg(
	    void *obj,
	    const char *creds,
	    uint32_t flags,
	    void *data,
	    size_t len,
	    void *addr_buf,
	    size_t addr_size,
	    size_t max_ctrl_len
	);

	static async::result<frg::expected<protocols::fs::Error>>
	setSocketOption(void *object, int layer, int number, std::vector<char> optbuf);

	static async::result<frg::expected<protocols::fs::Error, protocols::fs::PollWaitResult>>
	pollWait(void *obj, uint64_t past_seq, int mask, async::cancellation_token cancellation);

	static async::result<frg::expected<protocols::fs::Error, protocols::fs::PollStatusResult>>
	pollStatus(void *obj);

	constexpr static protocols::fs::FileOperations ops{
	    .write = &write,
	    .pollWait = &pollWait,
	    .pollStatus = &pollStatus,
	    .bind = &bind,
	    .recvMsg = &recvmsg,
	    .setSocketOption = &setSocketOption,
	};

  private:
	friend Raw;

	Raw *parent;
	smarter::weak_ptr<RawSocket> holder_;

	int proto;
	bool filterLocked_ = false;
	bool packetAuxData_ = false;
	std::optional<std::vector<char>> filter_ = std::nullopt;

	std::shared_ptr<nic::Link> link = {};

	struct PacketInfo {
		size_t len;
		arch::dma_buffer_view view;
	};

	async::queue<PacketInfo, frg::stl_allocator> queue_;

	async::recurring_event _statusBell;
	uint64_t _currentSeq;
	uint64_t _inSeq;
};

Raw &raw();
