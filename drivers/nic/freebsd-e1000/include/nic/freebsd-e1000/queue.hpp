#pragma once

#include <arch/dma_structs.hpp>
#include <async/oneshot-event.hpp>
#include <core/queue.hpp>
#include <stddef.h>

struct Request {
	async::oneshot_event event;
	arch::dma_buffer_view frame;
	size_t size;
};
