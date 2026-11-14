// Space Cadet Pinball for PS4: the platform layer (Platform/PS4).
//
//   ps4_main.cpp   log file, data folder, SDL hints; called first thing in main()
//   ps4_video.cpp  the dummy SDL video driver's window surface -> VideoOut (1080p or 720p, 3 buffers)
//   ps4_pad.cpp    DualShock 4s as SDL game controllers (virtual joysticks fed from ScePad)
//   ps4_ui.cpp     the pause menu (Options / touch pad), How to Play, the PS4 options
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Where settings, high scores and (when the package has none) the game's data live.
#define PS4_DATA_DIR "/data/SpaceCadetPinball/"
#define PS4_APP_DIR "/app0/"

void ps4_platform_init(void);            // before SDL_Init
void ps4_platform_after_sdl_init(void);  // after SDL_Init: pads
void ps4_video_init(void);
int ps4_video_supports(int width);       // a screen resolution VideoOut took buffers for (1920 or 1280)
int ps4_video_fps(void);                 // pictures shown in the last second
void ps4_pad_init(void);

#ifdef __cplusplus
}

namespace ps4ui
{
	// The menus' fonts (/app0/fonts), after ImGui::CreateContext and before the renderer takes the atlas.
	void LoadFonts();
	// Once the table is set up (each start of WinMain's loop): PS4 settings, screen size, frame rate.
	void Init();
	// Options or the touch pad: opens the pause menu (pausing the game), or closes it.
	void MenuButtonPressed();
	// Call between ImGui::NewFrame() and the UI, and after the UI before ImGui::Render().
	void AfterNewFrame();
	void BeforeRender();
	bool MenuActive();
	// After the table is drawn, before ImGui draws: darkens the menu's areas in the frame's pixels.
	void ShadeScreen();
	// A message box (no system one here): drawn until Cross.
	void ShowMessage(const char* title, const char* message);
}
#endif
