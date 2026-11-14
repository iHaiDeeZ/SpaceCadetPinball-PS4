// Space Cadet Pinball for PS4: DualShock 4s as SDL game controllers (from Conker's port, ps4/ps4_pad.cpp).
//
// SDL2 has no PS4 joystick driver, so each logged-in user's pad is a virtual joystick of the game
// controller type, filled from ScePad whenever SDL updates its joysticks. The game's own controller
// bindings (Options > Player Controls) then work as with any pad.
//
// Pinball wants digital flippers: L2 and R2 are folded into L1 and R1 (either one flips), so the
// default bindings (left/right shoulder) cover both pairs. The triggers are still reported as axes.
#include "ps4_platform.h"

#include <SDL.h>

#include <orbis/Pad.h>
#include <orbis/UserService.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

namespace
{
	constexpr int MAX_PADS = 4;
	constexpr uint8_t TRIGGER_PRESSED = 48; // of 255: a light pull flips

	struct Pad
	{
		int32_t handle = -1;
		int32_t user = -1;
		SDL_JoystickID joystick = -1;
		SDL_Joystick* opened = nullptr;
	};
	std::array<Pad, MAX_PADS> pads;

	// In SDL_GameControllerButton order: the virtual mapping numbers the buttons in mask order.
	struct ButtonMap
	{
		SDL_GameControllerButton button;
		uint32_t ps4;
	};
	constexpr ButtonMap BUTTONS[] = {
		{SDL_CONTROLLER_BUTTON_A, ORBIS_PAD_BUTTON_CROSS},
		{SDL_CONTROLLER_BUTTON_B, ORBIS_PAD_BUTTON_CIRCLE},
		{SDL_CONTROLLER_BUTTON_X, ORBIS_PAD_BUTTON_SQUARE},
		{SDL_CONTROLLER_BUTTON_Y, ORBIS_PAD_BUTTON_TRIANGLE},
		{SDL_CONTROLLER_BUTTON_START, ORBIS_PAD_BUTTON_OPTIONS},
		{SDL_CONTROLLER_BUTTON_LEFTSTICK, ORBIS_PAD_BUTTON_L3},
		{SDL_CONTROLLER_BUTTON_RIGHTSTICK, ORBIS_PAD_BUTTON_R3},
		{SDL_CONTROLLER_BUTTON_LEFTSHOULDER, ORBIS_PAD_BUTTON_L1 | ORBIS_PAD_BUTTON_L2},
		{SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, ORBIS_PAD_BUTTON_R1 | ORBIS_PAD_BUTTON_R2},
		{SDL_CONTROLLER_BUTTON_DPAD_UP, ORBIS_PAD_BUTTON_UP},
		{SDL_CONTROLLER_BUTTON_DPAD_DOWN, ORBIS_PAD_BUTTON_DOWN},
		{SDL_CONTROLLER_BUTTON_DPAD_LEFT, ORBIS_PAD_BUTTON_LEFT},
		{SDL_CONTROLLER_BUTTON_DPAD_RIGHT, ORBIS_PAD_BUTTON_RIGHT},
		// the touch pad's click (Share belongs to the system): opens the menu (ps4_ui.cpp)
		{SDL_CONTROLLER_BUTTON_TOUCHPAD, ORBIS_PAD_BUTTON_TOUCH_PAD},
	};

	// 0-255, centre 128
	Sint16 Stick(uint8_t value)
	{
		const int v = (static_cast<int>(value) - 128) * 256; // the game is C++11: no std::clamp
		return static_cast<Sint16>(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
	}

	Sint16 Trigger(uint8_t value)
	{
		return static_cast<Sint16>(value * 32767 / 255);
	}

	void SDLCALL Update(void* userdata)
	{
		Pad& pad = *static_cast<Pad*>(userdata);
		if (pad.handle < 0)
			return;
		OrbisPadData data{};
		if (scePadReadState(pad.handle, &data) != 0 || !data.connected)
			return;
		if (pad.opened == nullptr)
			pad.opened = SDL_JoystickFromInstanceID(pad.joystick);
		SDL_Joystick* joystick = pad.opened;
		if (joystick == nullptr)
			return;
		uint32_t held = data.buttons;
		if (data.analogButtons.l2 >= TRIGGER_PRESSED)
			held |= ORBIS_PAD_BUTTON_L2;
		if (data.analogButtons.r2 >= TRIGGER_PRESSED)
			held |= ORBIS_PAD_BUTTON_R2;
		int index = 0;
		for (const ButtonMap& b : BUTTONS)
			SDL_JoystickSetVirtualButton(joystick, index++, (held & b.ps4) ? SDL_PRESSED : SDL_RELEASED);
		SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, Stick(data.leftStick.x));
		SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTY, Stick(data.leftStick.y));
		SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_RIGHTX, Stick(data.rightStick.x));
		SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_RIGHTY, Stick(data.rightStick.y));
		SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT, Trigger(data.analogButtons.l2));
		SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, Trigger(data.analogButtons.r2));
	}

	int SDLCALL Rumble(void* userdata, Uint16 low, Uint16 high)
	{
		Pad& pad = *static_cast<Pad*>(userdata);
		if (pad.handle < 0)
			return -1;
		OrbisPadVibeParam vibe{};
		vibe.lgMotor = static_cast<uint8_t>(low >> 8);
		vibe.smMotor = static_cast<uint8_t>(high >> 8);
		return scePadSetVibration(pad.handle, &vibe) == 0 ? 0 : -1;
	}

	int SDLCALL SetLed(void* userdata, Uint8 red, Uint8 green, Uint8 blue)
	{
		Pad& pad = *static_cast<Pad*>(userdata);
		if (pad.handle < 0)
			return -1;
		OrbisPadColor color{red, green, blue, 0};
		return scePadSetLightBar(pad.handle, &color) == 0 ? 0 : -1;
	}

	void Attach(Pad& pad, int number)
	{
		SDL_VirtualJoystickDesc desc{};
		desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
		desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
		desc.naxes = SDL_CONTROLLER_AXIS_MAX;
		desc.nbuttons = static_cast<Uint16>(sizeof BUTTONS / sizeof BUTTONS[0]);
		// Sony's DualShock 4: SDL then calls it a PS4 controller (button names in the controls dialog)
		desc.vendor_id = 0x054C;
		desc.product_id = 0x09CC;
		for (const ButtonMap& b : BUTTONS)
			desc.button_mask |= 1u << b.button;
		desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
		desc.name = "DualShock 4";
		desc.userdata = &pad;
		desc.Update = Update;
		desc.Rumble = Rumble;
		desc.SetLED = SetLed;
		const int device = SDL_JoystickAttachVirtualEx(&desc);
		if (device < 0)
		{
			printf("PS4 pad %d: SDL_JoystickAttachVirtualEx failed: %s\n", number, SDL_GetError());
			return;
		}
		pad.joystick = SDL_JoystickGetDeviceInstanceID(device);
		printf("PS4 pad %d: user %X, handle %d, SDL joystick %d\n", number, static_cast<unsigned>(pad.user),
		       pad.handle, pad.joystick);
	}

	bool OpenPad(int32_t user, int slot)
	{
		const int32_t handle = scePadOpen(user, 0, 0, nullptr);
		if (handle < 0)
		{
			printf("PS4 pad: scePadOpen for user %X failed: %08X\n", static_cast<unsigned>(user),
			       static_cast<unsigned>(handle));
			return false;
		}
		pads[slot].user = user;
		pads[slot].handle = handle;
		Attach(pads[slot], slot + 1);
		return true;
	}
}

extern "C" void ps4_pad_init(void)
{
	sceUserServiceInitialize(nullptr);
	scePadInit();
	// every logged-in user's pad plays (the game takes any controller's buttons)
	int count = 0;
	OrbisUserServiceLoginUserIdList users{};
	if (sceUserServiceGetLoginUserIdList(&users) == 0)
	{
		for (int i = 0; i < 4 && count < MAX_PADS; i++)
		{
			const int32_t user = users.userId[i];
			if (user != -1 && user != 0 && OpenPad(user, count))
				count++;
		}
	}
	if (count == 0)
	{
		int32_t user = -1;
		sceUserServiceGetInitialUser(&user);
		if (!OpenPad(user, 0))
			printf("PS4 pad: no pad\n");
	}
}
