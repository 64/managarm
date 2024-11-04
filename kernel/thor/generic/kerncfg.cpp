#include <frg/string.hpp>

#include <thor-internal/coroutine.hpp>
#include <thor-internal/fiber.hpp>
#include <thor-internal/kerncfg.hpp>
#include <thor-internal/kernel-log.hpp>
#include <thor-internal/mbus.hpp>
#include <thor-internal/ostrace.hpp>
#include <thor-internal/physical.hpp>
#include <thor-internal/profile.hpp>
#include <thor-internal/stream.hpp>
#include <thor-internal/timer.hpp>
#include <thor-internal/universe.hpp>

#include "kerncfg.frigg_bragi.hpp"
#include <bragi/helpers-all.hpp>
#include <bragi/helpers-frigg.hpp>

#include <thor-internal/ring-buffer.hpp>

namespace thor {

extern frg::manual_box<frg::string<KernelAlloc>> kernelCommandLine;
extern frg::manual_box<LogRingBuffer> allocLog;

// ------------------------------------------------------------------------
// mbus object creation and management.
// ------------------------------------------------------------------------

namespace {

struct KerncfgBusObject : private KernelBusObject {
	coroutine<void> run() {
		Properties properties;
		properties.stringProperty("class", frg::string<KernelAlloc>(*kernelAlloc, "kerncfg"));

		// TODO(qookie): Better error handling here.
		(co_await createObject("kerncfg", std::move(properties))).unwrap();
	}

  private:
	coroutine<frg::expected<Error>> handleRequest(LaneHandle boundLane) override {
		auto [acceptError, lane] = co_await AcceptSender{boundLane};
		if (acceptError != Error::success)
			co_return acceptError;

		auto [reqError, reqBuffer] = co_await RecvBufferSender{lane};
		if (reqError != Error::success)
			co_return reqError;

		auto preamble = bragi::read_preamble(reqBuffer);
		if (preamble.error())
			co_return Error::protocolViolation;

		if (preamble.id() == bragi::message_id<managarm::kerncfg::GetCmdlineRequest>) {
			auto req = bragi::parse_head_only<managarm::kerncfg::GetCmdlineRequest>(
			    reqBuffer, *kernelAlloc
			);

			if (!req)
				co_return Error::protocolViolation;

			managarm::kerncfg::SvrResponse<KernelAlloc> resp(*kernelAlloc);
			resp.set_error(managarm::kerncfg::Error::SUCCESS);
			resp.set_size(kernelCommandLine->size());

			frg::unique_memory<KernelAlloc> respBuffer{*kernelAlloc, resp.size_of_head()};
			bragi::write_head_only(resp, respBuffer);
			auto respError = co_await SendBufferSender{lane, std::move(respBuffer)};
			if (respError != Error::success)
				co_return respError;

			frg::unique_memory<KernelAlloc> cmdlineBuffer{*kernelAlloc, kernelCommandLine->size()};
			memcpy(cmdlineBuffer.data(), kernelCommandLine->data(), kernelCommandLine->size());
			auto cmdlineError = co_await SendBufferSender{lane, std::move(cmdlineBuffer)};
			if (cmdlineError != Error::success)
				co_return cmdlineError;
		} else if (preamble.id() ==
		           bragi::message_id<managarm::kerncfg::GetMemoryInformationRequest>) {
			auto req = bragi::parse_head_only<managarm::kerncfg::GetMemoryInformationRequest>(
			    reqBuffer, *kernelAlloc
			);

			if (!req)
				co_return Error::protocolViolation;

			managarm::kerncfg::GetMemoryInformationResponse<KernelAlloc> resp(*kernelAlloc);
			resp.set_error(managarm::kerncfg::Error::SUCCESS);
			resp.set_total_usable_memory(physicalAllocator->numTotalPages());
			resp.set_available_memory(physicalAllocator->numFreePages());
			resp.set_memory_unit(kPageSize);

			frg::unique_memory<KernelAlloc> respBuffer{*kernelAlloc, resp.size_of_head()};
			bragi::write_head_only(resp, respBuffer);
			auto respError = co_await SendBufferSender{lane, std::move(respBuffer)};
			if (respError != Error::success)
				co_return respError;
		} else if (preamble.id() == bragi::message_id<managarm::kerncfg::GetNumCpuRequest>) {
			auto req = bragi::parse_head_only<managarm::kerncfg::GetNumCpuRequest>(
			    reqBuffer, *kernelAlloc
			);

			if (!req) {
				co_return Error::protocolViolation;
			}

			managarm::kerncfg::GetNumCpuResponse<KernelAlloc> resp(*kernelAlloc);
			resp.set_error(managarm::kerncfg::Error::SUCCESS);
			resp.set_num_cpu(getCpuCount());

			frg::unique_memory<KernelAlloc> respBuffer{*kernelAlloc, resp.size_of_head()};
			bragi::write_head_only(resp, respBuffer);
			auto respError = co_await SendBufferSender{lane, std::move(respBuffer)};
			if (respError != Error::success) {
				co_return respError;
			}
		} else {
			managarm::kerncfg::SvrResponse<KernelAlloc> resp(*kernelAlloc);
			resp.set_error(managarm::kerncfg::Error::ILLEGAL_REQUEST);

			frg::unique_memory<KernelAlloc> respBuffer{*kernelAlloc, resp.size_of_head()};
			bragi::write_head_only(resp, respBuffer);
			auto respError = co_await SendBufferSender{lane, std::move(respBuffer)};
			if (respError != Error::success)
				co_return respError;
		}

		co_return frg::success;
	}
};

struct ByteRingBusObject : private KernelBusObject {
	ByteRingBusObject(LogRingBuffer *buffer, frg::string_view purpose)
	    : buffer_{buffer},
	      purpose_{purpose} {}

	coroutine<void> run() {
		Properties properties;
		properties.stringProperty(
		    "class", frg::string<KernelAlloc>(*kernelAlloc, "kerncfg-byte-ring")
		);
		properties.stringProperty("purpose", frg::string<KernelAlloc>(*kernelAlloc, purpose_));

		// TODO(qookie): Better error handling here.
		(co_await createObject("kerncfg-byte-ring", std::move(properties))).unwrap();
	}

  private:
	LogRingBuffer *buffer_;
	frg::string_view purpose_;

	coroutine<frg::expected<Error>> handleRequest(LaneHandle boundLane) override {
		auto [acceptError, lane] = co_await AcceptSender{boundLane};
		if (acceptError != Error::success)
			co_return acceptError;

		auto [reqError, reqBuffer] = co_await RecvBufferSender{lane};
		if (reqError != Error::success)
			co_return reqError;

		auto preamble = bragi::read_preamble(reqBuffer);
		if (preamble.error())
			co_return Error::protocolViolation;

		if (preamble.id() == bragi::message_id<managarm::kerncfg::GetBufferContentsRequest>) {
			auto maybeReq = bragi::parse_head_only<managarm::kerncfg::GetBufferContentsRequest>(
			    reqBuffer, *kernelAlloc
			);

			if (!maybeReq)
				co_return Error::protocolViolation;

			auto &req = *maybeReq;
			managarm::kerncfg::SvrResponse<KernelAlloc> resp(*kernelAlloc);
			resp.set_error(managarm::kerncfg::Error::SUCCESS);

			frg::unique_memory<KernelAlloc> dataBuffer{*kernelAlloc, req.size()};

			size_t progress = 0;

			// Extract the first record. We stop on success.
			uint64_t effectivePtr;
			uint64_t currentPtr;
			while (true) {
				auto [success, recordPtr, nextPtr, actualSize] =
				    buffer_->dequeueAt(req.dequeue(), dataBuffer.data(), req.size());
				if (success) {
					assert(actualSize); // For now, we do not support size zero records.
					if (actualSize == req.size())
						infoLogger()
						    << "thor: kerncfg truncates a ring buffer record" << frg::endlog;
					effectivePtr = recordPtr;
					currentPtr = nextPtr;
					progress += actualSize;
					break;
				}

				if (!(req.flags() & managarm::kerncfg::GetBufferContentsFlags::NO_WAIT))
					co_await buffer_->wait(nextPtr);
				else {
					resp.set_error(managarm::kerncfg::Error::WOULD_BLOCK);
					break;
				}
			}

			if (!(req.flags() & managarm::kerncfg::GetBufferContentsFlags::ONE_RECORD)) {
				// Extract further records. We stop on failure, or if we miss records.
				while (true) {
					auto [success, recordPtr, nextPtr, actualSize] = buffer_->dequeueAt(
					    currentPtr,
					    static_cast<std::byte *>(dataBuffer.data()) + progress,
					    req.size() - progress
					);
					if (recordPtr != currentPtr)
						break;
					if (success) {
						assert(actualSize); // For now, we do not support size zero records.
						if (actualSize == req.size() - progress)
							break;
						currentPtr = nextPtr;
						progress += actualSize;
						continue;
					}

					if (progress >= req.watermark() ||
					    req.flags() & managarm::kerncfg::GetBufferContentsFlags::NO_WAIT)
						break;

					co_await buffer_->wait(nextPtr);
				}
			}

			resp.set_size(progress);
			resp.set_effective_dequeue(effectivePtr);
			resp.set_new_dequeue(currentPtr);

			frg::unique_memory<KernelAlloc> respBuffer{*kernelAlloc, resp.size_of_head()};
			bragi::write_head_only(resp, respBuffer);
			auto respError = co_await SendBufferSender{lane, std::move(respBuffer)};
			if (respError != Error::success)
				co_return respError;

			auto dataError = co_await SendBufferSender{lane, std::move(dataBuffer)};

			if (dataError != Error::success)
				co_return dataError;
		} else {
			managarm::kerncfg::SvrResponse<KernelAlloc> resp(*kernelAlloc);
			resp.set_error(managarm::kerncfg::Error::ILLEGAL_REQUEST);

			frg::unique_memory<KernelAlloc> respBuffer{*kernelAlloc, resp.size_of_head()};
			bragi::write_head_only(resp, respBuffer);
			auto respError = co_await SendBufferSender{lane, std::move(respBuffer)};
			if (respError != Error::success)
				co_return respError;
		}

		co_return frg::success;
	}
};

} // anonymous namespace

void initializeKerncfg() {
	// Create a fiber to manage requests to the kerncfg mbus object(s).
	KernelFiber::run([=] {
		auto kerncfg = frg::construct<KerncfgBusObject>(*kernelAlloc);
		async::detach_with_allocator(*kernelAlloc, kerncfg->run());

		{
			auto ring =
			    frg::construct<ByteRingBusObject>(*kernelAlloc, getGlobalLogRing(), "kernel-log");
			async::detach_with_allocator(*kernelAlloc, ring->run());
		}

#ifdef KERNEL_LOG_ALLOCATIONS
		auto ring = frg::construct<ByteRingBusObject>(*kernelAlloc, allocLog.get(), "heap-trace");
		async::detach_with_allocator(*kernelAlloc, ring->run());
#endif

		if (wantKernelProfile) {
			auto ring = frg::construct<ByteRingBusObject>(
			    *kernelAlloc, getGlobalProfileRing(), "kernel-profile"
			);
			async::detach_with_allocator(*kernelAlloc, ring->run());
		}

		if (wantOsTrace) {
			auto ring =
			    frg::construct<ByteRingBusObject>(*kernelAlloc, getGlobalOsTraceRing(), "os-trace");
			async::detach_with_allocator(*kernelAlloc, ring->run());
		}
	});
}

} // namespace thor
