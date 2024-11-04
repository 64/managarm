/* This file is derived from fuchsia's `fuchsia.c` and has been adapted for use in managarm.
 * The license is preserved below. For the original file, look here:
 * https://fuchsia.googlesource.com/fuchsia/+/a10b58da9f068289236785bd2f707142ca13798a/zircon/third_party/dev/ethernet/e1000/fuchsia.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2016 Nicole Graziano <nicole@nextbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <async/basic.hpp>
#include <helix/ipc.hpp>
#include <memory>
#include <net/ethernet.h>
#include <nic/freebsd-e1000/common.hpp>
#include <unistd.h>

E1000Nic::E1000Nic(protocols::hw::Device device)
    : nic::Link(1500, &_dmaPool),
      _device{std::move(device)},
      _rxIndex(0, RX_QUEUE_SIZE),
      _txIndex(0, TX_QUEUE_SIZE) {
	async::run(this->init(), helix::currentDispatcher);
}

async::result<void> E1000Nic::init() {
	u32 reg_rctl = 0;

	auto info = co_await _device.getPciInfo();
	_irq = co_await _device.accessIrq();
	co_await _device.enableBusmaster();

	co_await identifyHardware();

	auto &barInfo = info.barInfo[0];
	assert(barInfo.ioType == protocols::hw::IoType::kIoTypeMemory);
	auto bar0 = co_await _device.accessBar(0);

	_mmio_mapping = {bar0, barInfo.offset, barInfo.length};
	_mmio = _mmio_mapping.get();

	_hw.back = &_osdep;
	_osdep.membase = uintptr_t(_mmio_mapping.get());
	_hw.hw_addr = reinterpret_cast<u8 *>(_mmio_mapping.get());
	e1000_osdep_set_pci(&_osdep, _device);

	/* Only older adapters use IO mapping */
	if (_hw.mac.type < em_mac_min && _hw.mac.type > e1000_82543) {
		/* Figure our where our IO BAR is. We've already mapped the first BAR as
		 * MMIO, so it must be one of the remaining five. */
		bool found_io_bar = false;
		for (uint32_t i = 1; i < 6; i++) {
			if (info.barInfo[i].ioType == protocols::hw::kIoTypePort) {
				_osdep.iobase = info.barInfo[i].address;
				_hw.io_base = 0;
				auto bar = co_await _device.accessBar(i);
				HEL_CHECK(helEnableIo(bar.getHandle()));
				_io = {static_cast<uint16_t>(info.barInfo[0].address)};

				found_io_bar = true;
				break;
			}
		}
		if (!found_io_bar) {
			printf("e1000: Unable to locate IO BAR");
			goto fail;
		}
	}

	if (_hw.mac.type >= igb_mac_min) {
		type = NicType::Igb;
		assert(!"unimplemented or untested");
	} else if (_hw.mac.type >= em_mac_min)
		type = NicType::Em;
	else
		type = NicType::Lem;

	/*
	 * For ICH8 and family we need to
	 * map the flash memory, and this
	 * must happen after the MAC is
	 * identified
	 */
	if ((_hw.mac.type == e1000_ich8lan) || (_hw.mac.type == e1000_ich9lan) ||
	    (_hw.mac.type == e1000_ich10lan) || (_hw.mac.type == e1000_pchlan) ||
	    (_hw.mac.type == e1000_pch2lan) || (_hw.mac.type == e1000_pch_lpt)) {
		printf("e1000: Mapping of flash unimplemented\n");
		goto fail;
	} else if (_hw.mac.type >= e1000_pch_spt) {
		/**
		 * In the new SPT device flash is not a separate BAR, rather it is also in BAR0,
		 * so use the same tag and an offset handle for the FLASH read/write macros in the shared
		 * code.
		 */

		hw2flashbase(&_hw) = ((struct e1000_osdep *)_hw.back)->membase + E1000_FLASH_BASE_ADDR;
	}

	{
		auto init_ret = e1000_setup_init_funcs(&_hw, true);
		assert(init_ret == E1000_SUCCESS);
	}

	e1000_get_bus_info(&_hw);

	_hw.mac.autoneg = 1;
	_hw.phy.autoneg_wait_to_complete = false;
	_hw.phy.autoneg_advertised =
	    (ADVERTISE_10_HALF | ADVERTISE_10_FULL | ADVERTISE_100_HALF | ADVERTISE_100_FULL |
	     ADVERTISE_1000_FULL);

	if (_hw.phy.media_type == e1000_media_type_copper) {
		_hw.phy.mdix = 0;
		_hw.phy.disable_polarity_correction = false;
		_hw.phy.ms_type = e1000_ms_hw_default;
	}

	_hw.mac.report_tx_early = 1;

	if (e1000_check_reset_block(&_hw)) {
		DEBUGOUT("PHY reset is blocked due to SOL/IDER session.");
	}

	/*
	 * Start from a known state, this is
	 * important in reading the nvm and
	 * mac from that.eth_queue_tx
	 */
	e1000_reset_hw(&_hw);
	e1000_power_up_phy(&_hw);

	if (e1000_validate_nvm_checksum(&_hw) < 0) {
		if (e1000_validate_nvm_checksum(&_hw) < 0) {
			std::cout << "e1000: EEPROM checksum not valid\n";
			goto fail;
		}
	}

	if (e1000_read_mac_addr(&_hw) < 0) {
		std::cout << "e1000: error while reading MAC address" << std::endl;
		goto fail;
	}

	for (size_t i = 0; i < ETHER_ADDR_LEN; i++) {
		mac_[i] = _hw.mac.addr[i];
	}

	e1000_disable_ulp_lpt_lp(&_hw, true);

	_rxd = arch::dma_array<struct e1000_rx_desc>(dmaPool_, RX_QUEUE_SIZE);
	_rxdbuf = arch::dma_array<DescriptorSpace>(dmaPool_, RX_QUEUE_SIZE);

	memset(_rxd.data(), 0, RX_QUEUE_SIZE * sizeof(struct e1000_rx_desc));

	if (_hw.mac.type >= em_mac_min) {
		em_rxd_setup();
	} else {
		for (size_t i = 0; i < RX_QUEUE_SIZE; i++) {
			_rxd[i].buffer_addr = helix_ng::ptrToPhysical(&_rxdbuf[i]);
			_rxd[i].length = 2048;
		}
	}

	_txd = arch::dma_array<struct e1000_tx_desc>(dmaPool_, TX_QUEUE_SIZE);
	_txdbuf = arch::dma_array<DescriptorSpace>(dmaPool_, TX_QUEUE_SIZE);

	memset(_txd.data(), 0, TX_QUEUE_SIZE * sizeof(struct e1000_tx_desc));

	for (size_t i = 0; i < TX_QUEUE_SIZE; i++) {
		_txd[i].buffer_addr = helix_ng::ptrToPhysical(&_txdbuf[i]);
		_txd[i].lower.data = 0;
		_txd[i].upper.data = 0;
	}

	co_await txInit();
	co_await rxInit();

	reg_rctl = E1000_READ_REG(&_hw, E1000_RCTL);
	reg_rctl &= (~E1000_RCTL_SBP);
	reg_rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
	E1000_WRITE_REG(&_hw, E1000_RCTL, reg_rctl);

	e1000_clear_hw_cntrs_base_generic(&_hw);

	E1000_WRITE_REG(&_hw, E1000_IMS, IMS_ENABLE_MASK);

	processIrqs();

	co_return;

fail:
	std::cout << "e1000: FAIL" << std::endl;
	co_return;
}

async::result<size_t> E1000Nic::receive(arch::dma_buffer_view frame) {
	Request req{.frame = frame};
	_requests.push(&req);

	eth_rx_pop();

	co_await req.event.wait();

	co_return req.size;
}

async::result<void> E1000Nic::send(const arch::dma_buffer_view buf) {
	reap_tx_buffers();

	memcpy(&_txdbuf[_txIndex], buf.data(), buf.size());
	struct e1000_tx_desc *desc = &_txd[_txIndex];
	desc->lower.data = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS | buf.size();

	++_txIndex;
	E1000_WRITE_REG(&_hw, E1000_TDT(0), _txIndex);

	// TODO(no92): decide whether returning without waiting for the TX IRQ is optimal
	co_return;
}

async::result<void> E1000Nic::identifyHardware() {
	_hw.vendor_id = co_await _device.loadPciSpace(0, 2);
	_hw.device_id = co_await _device.loadPciSpace(2, 2);
	_hw.revision_id = co_await _device.loadPciSpace(8, 1);
	_hw.subsystem_vendor_id = co_await _device.loadPciSpace(0x2C, 2);
	_hw.subsystem_device_id = co_await _device.loadPciSpace(0x2E, 2);

	auto ret = e1000_set_mac_type(&_hw);
	assert(ret == E1000_SUCCESS);

	printf("e1000: using PCI device %04x:%04x\n", _hw.vendor_id, _hw.device_id);
}

void E1000Nic::pciRead(u32 reg, u32 *value) {
	auto run = [this, reg]() -> async::result<u16> {
		u32 ret = co_await _device.loadPciSpace(reg, 4);
		co_return ret;
	};

	*value = async::run(run(), helix::currentDispatcher);
}

void E1000Nic::pciRead(u32 reg, u16 *value) {
	auto run = [this, reg]() -> async::result<u16> {
		u16 ret = co_await _device.loadPciSpace(reg, 2);
		co_return ret;
	};

	*value = async::run(run(), helix::currentDispatcher);
}

void E1000Nic::pciRead(u32 reg, u8 *value) {
	auto run = [this, reg]() -> async::result<u16> {
		u8 ret = co_await _device.loadPciSpace(reg, 1);
		co_return ret;
	};

	*value = async::run(run(), helix::currentDispatcher);
}

namespace nic::e1000 {

std::shared_ptr<nic::Link> makeShared(protocols::hw::Device device) {
	return std::make_shared<E1000Nic>(std::move(device));
}

} // namespace nic::e1000
