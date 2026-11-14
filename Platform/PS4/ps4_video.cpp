// Space Cadet Pinball for PS4: the picture.
//
// SDL's dummy video driver keeps the window's pixels in an ordinary surface, which the software
// renderer draws the table and the menus into. On each SDL_RenderPresent the patched driver calls
// SDL_PS4_PresentFramebuffer, and the surface is copied into one of three write-combined VideoOut
// buffers and flipped on the next vertical blank. With three buffers the game loop only waits when it
// gets two frames ahead of the TV.
//
// Two sets of buffers: 1920x1080, and 1280x720 for the faster screen resolution (Options), which the
// PS4 scales to the TV itself, so the CPU draws and copies 2.25 times fewer pixels. The window's size
// picks the set; a window smaller than the set is centred.
#include "ps4_platform.h"

#include <SDL.h>
#include <SDL_ps4framebuffer.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <orbis/SystemService.h>
#include <orbis/VideoOut.h>

extern "C" {
int32_t ps4v_allocate_direct(off_t start, off_t end, size_t len, size_t align, int32_t type, off_t* phys) __asm__("sceKernelAllocateDirectMemory");
int32_t ps4v_map_direct(void** addr, size_t len, int32_t prot, int32_t flags, off_t phys, size_t align) __asm__("sceKernelMapDirectMemory");
size_t ps4v_direct_memory_size() __asm__("sceKernelGetDirectMemorySize");
}

namespace
{
	constexpr int BUFFERS = 3;
	struct BufferSet
	{
		int w, h;
		int first; // VideoOut buffer index of its first buffer
		bool ready;
		uint8_t* buffers[BUFFERS];
		int clearedW[BUFFERS], clearedH[BUFFERS]; // the picture's rectangle; the rest is black
	};
	BufferSet sets[2] = {{1920, 1080, 0}, {1280, 720, BUFFERS}};
	constexpr int SET_COUNT = 2;
	constexpr size_t BUFFER_SIZE = 8ull << 20; // 1920x1080x4 rounded up to 2 MiB pages
	constexpr int32_t WC_GARLIC = 3;            // write-combined: what the display reads
	constexpr int32_t PROT_CPU_GPU_RW = 0x33;
	// B, G, R, A bytes: the same order as SDL's RGB888 surface, so rows are copied as they are
	constexpr uint32_t PIXEL_FORMAT_A8R8G8B8_SRGB = 0x80000000;
	constexpr int64_t FLIP_ARG_BASE = 0x5C900000;

	int video = -1;
	int64_t flips = 0;
	bool splashHidden = false;
	int lastSet = -1;
	// the FPS counter: pictures shown per second
	Uint32 fpsStart = 0;
	int fpsFrames = 0, fpsShown = 0;

	// Until the flip before the last one is off the screen: then the buffer it showed is free.
	void WaitForBuffer()
	{
		if (flips < BUFFERS - 1)
			return;
		const int64_t need = FLIP_ARG_BASE + flips - (BUFFERS - 2);
		for (int i = 0; i < 100; i++)
		{
			OrbisVideoOutFlipStatus status{};
			if (sceVideoOutGetFlipStatus(video, &status) < 0 || status.flipArg >= need)
				return;
			const timespec pause = {0, 500000};
			nanosleep(&pause, nullptr);
		}
	}

	void Present(const void* pixels, int w, int h, int pitch)
	{
		if (video < 0)
			return;
		WaitForBuffer();
		// the smallest set the window fits in
		int si = 0;
		for (int i = 0; i < SET_COUNT; i++)
			if (sets[i].ready && w <= sets[i].w && h <= sets[i].h)
				si = i;
		BufferSet& set = sets[si];
		if (si != lastSet)
		{
			printf("PS4 video: %dx%d window on the %dx%d buffers\n", w, h, set.w, set.h);
			lastSet = si;
		}
		const int index = static_cast<int>(flips % BUFFERS);
		uint8_t* dst = set.buffers[index];
		const int cw = std::min(w, set.w), ch = std::min(h, set.h);
		const int x0 = (set.w - cw) / 2, y0 = (set.h - ch) / 2;
		if (set.clearedW[index] != cw || set.clearedH[index] != ch)
		{
			std::memset(dst, 0, static_cast<size_t>(set.w) * set.h * 4);
			set.clearedW[index] = cw;
			set.clearedH[index] = ch;
		}
		const auto* src = static_cast<const uint8_t*>(pixels);
		for (int y = 0; y < ch; y++)
			std::memcpy(dst + (static_cast<size_t>(y0 + y) * set.w + x0) * 4, src + static_cast<size_t>(y) * pitch,
			            static_cast<size_t>(cw) * 4);
		__builtin_ia32_sfence(); // the write-combined stores out before the flip
		flips++;
		const int flip = sceVideoOutSubmitFlip(video, set.first + index, ORBIS_VIDEO_OUT_FLIP_VSYNC, FLIP_ARG_BASE + flips);
		fpsFrames++;
		const Uint32 now = SDL_GetTicks();
		if (now - fpsStart >= 1000)
		{
			fpsShown = static_cast<int>(fpsFrames * 1000 / (now - fpsStart));
			fpsFrames = 0;
			fpsStart = now;
		}
		if (!splashHidden)
		{
			printf("PS4 video: first frame %dx%d (flip %08x)\n", w, h, static_cast<unsigned>(flip));
			// the system shows the package's splash until the app has a picture of its own
			sceSystemServiceHideSplashScreen();
			splashHidden = true;
		}
	}
}

extern "C" void ps4_video_init(void)
{
	video = sceVideoOutOpen(0xff, 0, 0, nullptr);
	if (video < 0)
	{
		printf("PS4 video: sceVideoOutOpen failed (%08x)\n", static_cast<unsigned>(video));
		return;
	}
	// both sets in one allocation: 2 x 3 buffers
	off_t phys = 0;
	void* base = nullptr;
	const size_t total = SET_COUNT * BUFFERS * BUFFER_SIZE;
	const int32_t allocated = ps4v_allocate_direct(0, static_cast<off_t>(ps4v_direct_memory_size()), total, 2 << 20,
	                                               WC_GARLIC, &phys);
	const int32_t mapped = allocated != 0 ? -1 : ps4v_map_direct(&base, total, PROT_CPU_GPU_RW, 0, phys, 2 << 20);
	if (allocated != 0 || mapped != 0)
	{
		printf("PS4 video: no direct memory for the screen (%08x %08x)\n", static_cast<unsigned>(allocated),
		       static_cast<unsigned>(mapped));
		video = -1;
		return;
	}
	std::memset(base, 0, total);
	for (int s = 0; s < SET_COUNT; s++)
	{
		BufferSet& set = sets[s];
		void* addresses[BUFFERS];
		for (int i = 0; i < BUFFERS; i++)
			addresses[i] = set.buffers[i] = static_cast<uint8_t*>(base) + (s * BUFFERS + i) * BUFFER_SIZE;
		OrbisVideoOutBufferAttribute attribute;
		sceVideoOutSetBufferAttribute(&attribute, PIXEL_FORMAT_A8R8G8B8_SRGB, ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, 0,
		                              set.w, set.h, set.w);
		const int r = sceVideoOutRegisterBuffers(video, set.first, addresses, BUFFERS, &attribute);
		set.ready = r >= 0;
		printf("PS4 video: %dx%d buffers %d-%d: %s (%08x)\n", set.w, set.h, set.first, set.first + BUFFERS - 1,
		       set.ready ? "ready" : "refused", static_cast<unsigned>(r));
	}
	if (!sets[0].ready)
	{
		video = -1;
		return;
	}
	sceVideoOutSetFlipRate(video, 0); // 60 Hz
	SDL_PS4_PresentFramebuffer = Present;
}

extern "C" int ps4_video_supports(int width)
{
	for (const BufferSet& set : sets)
		if (set.w == width)
			return set.ready ? 1 : 0;
	return 0;
}

extern "C" int ps4_video_fps(void)
{
	return fpsShown;
}
