// Space Cadet Pinball for PS4: start-up (see ps4_platform.h).
#include "ps4_platform.h"

#include <SDL.h>
#include <SDL_mixer.h>

#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <orbis/SystemService.h>

extern "C" void ps4_platform_init(void)
{
	mkdir("/data/SpaceCadetPinball", 0777);

	// Everything the game prints, for reports: the previous run's log is kept.
	rename(PS4_DATA_DIR "log.txt", PS4_DATA_DIR "log-prev.txt");
	// The file goes under descriptors 1 and 2, which stdout and stderr write to. (v00.01-02 reopened
	// stdout with freopen and the log stayed empty on the console.)
	const int log = open(PS4_DATA_DIR "log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (log >= 0)
	{
		dup2(log, 1);
		dup2(log, 2);
		if (log > 2)
			close(log);
	}
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	printf("Space Cadet Pinball for PS4 %s\n", PINBALL_PS4_VERSION);

	// SDL has no PS4 video driver: the dummy one's window surface goes to VideoOut (ps4_video.cpp).
	SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
	SDL_SetHint(SDL_HINT_AUDIODRIVER, "ps4");
	// The software renderer draws the table and the menus; nothing to ask of a GPU driver.
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
	SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
	// The music (PINBALL.MID) plays on SDL_mixer's built-in Timidity, with the package's freepats.
	Mix_SetTimidityCfg(PS4_APP_DIR "timidity/timidity.cfg");
	ps4_video_init();
}

extern "C" void ps4_platform_after_sdl_init(void)
{
	ps4_pad_init();
}
