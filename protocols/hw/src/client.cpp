
#include <vector>

#include <string.h>

#include <frg/std_compat.hpp>

#include "protocols/hw/client.hpp"
#include <bragi/helpers-all.hpp>
#include <bragi/helpers-std.hpp>
#include <helix/ipc.hpp>
#include <hw.bragi.hpp>

namespace protocols::hw {

async::result<PciInfo> Device::getPciInfo() {
	managarm::hw::GetPciInfoRequest req;

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(), helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size())
	);

	HEL_CHECK(recv_tail.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);

	PciInfo info{};
	info.numMsis = resp.num_msis();

	for (size_t i = 0; i < resp.capabilities_size(); i++)
		info.caps.push_back({resp.capabilities(i).type()});

	for (size_t i = 0; i < 6; i++) {
		if (i >= resp.bars_size()) {
			info.barInfo[i].ioType = IoType::kIoTypeNone;
			info.barInfo[i].hostType = IoType::kIoTypeNone;
			info.barInfo[i].address = 0;
			info.barInfo[i].length = 0;
			info.barInfo[i].offset = 0;
			continue;
		}

		if (resp.bars(i).io_type() == managarm::hw::IoType::NO_BAR) {
			info.barInfo[i].ioType = IoType::kIoTypeNone;
		} else if (resp.bars(i).io_type() == managarm::hw::IoType::PORT) {
			info.barInfo[i].ioType = IoType::kIoTypePort;
		} else if (resp.bars(i).io_type() == managarm::hw::IoType::MEMORY) {
			info.barInfo[i].ioType = IoType::kIoTypeMemory;
		} else {
			throw std::runtime_error("Illegal IoType for io_type!\n");
		}

		if (resp.bars(i).host_type() == managarm::hw::IoType::NO_BAR) {
			info.barInfo[i].hostType = IoType::kIoTypeNone;
		} else if (resp.bars(i).host_type() == managarm::hw::IoType::PORT) {
			info.barInfo[i].hostType = IoType::kIoTypePort;
		} else if (resp.bars(i).host_type() == managarm::hw::IoType::MEMORY) {
			info.barInfo[i].hostType = IoType::kIoTypeMemory;
		} else {
			throw std::runtime_error("Illegal IoType for host_type!\n");
		}
		info.barInfo[i].address = resp.bars(i).address();
		info.barInfo[i].length = resp.bars(i).length();
		info.barInfo[i].offset = resp.bars(i).offset();
	}

	info.expansionRomInfo.address = resp.expansion_rom().address();
	info.expansionRomInfo.length = resp.expansion_rom().length();

	co_return info;
}

async::result<helix::UniqueDescriptor> Device::accessBar(int index) {
	managarm::hw::AccessBarRequest req;
	req.set_index(index);

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail, pull_bar] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(),
	    helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size()),
	    helix_ng::pullDescriptor()
	);

	HEL_CHECK(recv_tail.error());
	HEL_CHECK(pull_bar.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);

	auto bar = pull_bar.descriptor();
	co_return std::move(bar);
}

async::result<helix::UniqueDescriptor> Device::accessExpansionRom() {
	managarm::hw::AccessExpansionRomRequest req;

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail, pull_bar] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(),
	    helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size()),
	    helix_ng::pullDescriptor()
	);

	HEL_CHECK(recv_tail.error());
	HEL_CHECK(pull_bar.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);

	auto expansion_rom = pull_bar.descriptor();
	co_return std::move(expansion_rom);
}

async::result<helix::UniqueDescriptor> Device::accessIrq(size_t index) {
	managarm::hw::AccessIrqRequest req;
	req.set_index(index);

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail, pull_irq] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(),
	    helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size()),
	    helix_ng::pullDescriptor()
	);

	HEL_CHECK(recv_tail.error());
	HEL_CHECK(pull_irq.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);

	co_return pull_irq.descriptor();
}

async::result<helix::UniqueDescriptor> Device::installMsi(int index) {
	managarm::hw::InstallMsiRequest req;
	req.set_index(index);

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail, pull_msi] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(),
	    helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size()),
	    helix_ng::pullDescriptor()
	);

	HEL_CHECK(recv_tail.error());
	HEL_CHECK(pull_msi.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);

	co_return pull_msi.descriptor();
}

async::result<void> Device::claimDevice() {
	managarm::hw::ClaimDeviceRequest req;

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(), helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size())
	);

	HEL_CHECK(recv_tail.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);
}

async::result<void> Device::enableBusIrq() {
	managarm::hw::EnableBusIrqRequest req;

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(), helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size())
	);

	HEL_CHECK(recv_tail.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);
}

async::result<void> Device::enableMsi() {
	managarm::hw::EnableMsiRequest req;

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(), helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size())
	);

	HEL_CHECK(recv_tail.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);
}

async::result<void> Device::enableBusmaster() {
	managarm::hw::EnableBusmasterRequest req;

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(), helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size())
	);

	HEL_CHECK(recv_tail.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);
}

async::result<uint32_t> Device::loadPciSpace(size_t offset, unsigned int size) {
	managarm::hw::LoadPciSpaceRequest req;
	req.set_offset(offset);
	req.set_size(size);

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(), helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size())
	);

	HEL_CHECK(recv_tail.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);

	co_return resp.word();
}

async::result<void> Device::storePciSpace(size_t offset, unsigned int size, uint32_t word) {
	managarm::hw::StorePciSpaceRequest req;
	req.set_offset(offset);
	req.set_size(size);
	req.set_word(word);

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(), helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size())
	);

	HEL_CHECK(recv_tail.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);
}

async::result<uint32_t>
Device::loadPciCapability(unsigned int index, size_t offset, unsigned int size) {
	managarm::hw::LoadPciCapabilityRequest req;
	req.set_index(index);
	req.set_offset(offset);
	req.set_size(size);

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(), helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size())
	);

	HEL_CHECK(recv_tail.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);

	co_return resp.word();
}

async::result<FbInfo> Device::getFbInfo() {
	managarm::hw::GetFbInfoRequest req;

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(), helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size())
	);

	HEL_CHECK(recv_tail.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);

	FbInfo info;

	info.pitch = resp.fb_pitch();
	info.width = resp.fb_width();
	info.height = resp.fb_height();
	info.bpp = resp.fb_bpp();
	info.type = resp.fb_type();

	co_return info;
}

async::result<helix::UniqueDescriptor> Device::accessFbMemory() {
	managarm::hw::AccessFbMemoryRequest req;

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail, pull_bar] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(),
	    helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size()),
	    helix_ng::pullDescriptor()
	);

	HEL_CHECK(recv_tail.error());
	HEL_CHECK(pull_bar.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::SvrResponse>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);

	auto bar = pull_bar.descriptor();
	co_return std::move(bar);
}

async::result<void> Device::getBatteryState(BatteryState &state, bool block) {
	managarm::hw::BatteryStateRequest req;
	req.set_block_until_ready(block);

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(), helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size())
	);

	HEL_CHECK(recv_tail.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::BatteryStateReply>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);

	auto copy_state = []<typename T>(std::optional<T> &state, T data) {
		if (data)
			state = data;
		else
			state = std::nullopt;
	};

	state.charging = (resp.charging() != 0);
	copy_state(state.current_now, resp.current_now());
	copy_state(state.power_now, resp.power_now());
	copy_state(state.energy_now, resp.energy_now());
	copy_state(state.energy_full, resp.energy_full());
	copy_state(state.energy_full_design, resp.energy_full_design());
	copy_state(state.voltage_now, resp.voltage_now());
	copy_state(state.voltage_min_design, resp.voltage_min_design());

	co_return;
}

async::result<std::shared_ptr<AcpiResources>> Device::getResources() {
	managarm::hw::AcpiGetResourcesRequest req;

	auto [offer, send_req, recv_head] = co_await helix_ng::exchangeMsgs(
	    _lane,
	    helix_ng::offer(
	        helix_ng::want_lane,
	        helix_ng::sendBragiHeadOnly(req, frg::stl_allocator{}),
	        helix_ng::recvInline()
	    )
	);

	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_head.error());

	auto preamble = bragi::read_preamble(recv_head);
	assert(!preamble.error());
	recv_head.reset();

	std::vector<std::byte> tailBuffer(preamble.tail_size());
	auto [recv_tail] = co_await helix_ng::exchangeMsgs(
	    offer.descriptor(), helix_ng::recvBuffer(tailBuffer.data(), tailBuffer.size())
	);

	HEL_CHECK(recv_tail.error());

	auto resp = *bragi::parse_head_tail<managarm::hw::AcpiGetResourcesReply>(recv_head, tailBuffer);

	assert(resp.error() == managarm::hw::Errors::SUCCESS);
	auto res = std::make_shared<AcpiResources>();
	res->io_ports = resp.io_ports();
	res->irqs = resp.irqs();

	co_return res;
}

} // namespace protocols::hw
