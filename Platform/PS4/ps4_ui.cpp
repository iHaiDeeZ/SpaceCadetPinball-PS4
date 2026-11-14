// Space Cadet Pinball for PS4: the pause menu, How to Play and the PS4 options.
//
// On the PC the game's menus are an ImGui menu bar made for a mouse. On the PS4 the bar is hidden and
// everything is in one console-style menu, opened with Options (or the touch pad), which pauses the game:
//
//   Paused        Resume / New Game / Launch Ball / How to Play / Options / High Scores / Quit Game
//   How to Play   what to do, and every button (drawn PlayStation button icons)
//   Options       audio, display, gameplay, button assignment (the game's own controls dialog)
//
// Look: the paused table dimmed, a gradient panel on the left with the title in Orbitron, large rows
// in Exo 2 with an accent bar on the focused one, a hint line for the focused row and a prompt bar of
// buttons at the bottom. Fonts are Platform/PS4/fonts (SIL OFL), loaded from /app0/fonts; without them
// the menus fall back to ImGui's own font. Everything is drawn at 1080p sizes times Scale().
//
// Rows are Selectables (ImGui's pad navigation): d-pad or left stick to move, Cross to pick or toggle,
// left/right to change a value, Circle to go back. PS4-only settings (resolution, frame rate, FPS
// counter, help seen) are in /data/SpaceCadetPinball/ps4.cfg; the game's own in its imgui_pb.ini.
#include "pch.h"
#include "winmain.h"
#include "options.h"
#include "pb.h"
#include "Sound.h"
#include "midi.h"
#include "fullscrn.h"
#include "translations.h"
#include "ps4_platform.h"

#include <cstdlib>

namespace
{
	// --- look -------------------------------------------------------------------------------------
	const ImU32 ACCENT = IM_COL32(64, 196, 255, 255);     // cyan
	const ImU32 ACCENT2 = IM_COL32(190, 110, 255, 255);   // violet, as the table's art
	const ImU32 TEXT = IM_COL32(236, 240, 250, 255);
	const ImU32 TEXT_DIM = IM_COL32(150, 160, 185, 255);
	const ImU32 TEXT_OFF = IM_COL32(90, 98, 118, 255);
	const ImU32 PANEL = IM_COL32(6, 10, 24, 238);

	ImFont* fontBody = nullptr;   // Exo 2 Regular 30
	ImFont* fontItem = nullptr;   // Exo 2 SemiBold 40
	ImFont* fontSmall = nullptr;  // Exo 2 SemiBold 24
	ImFont* fontTitle = nullptr;  // Orbitron Bold 76
	ImFont* fontHeader = nullptr; // Orbitron Bold 28

	enum class Btn
	{
		Cross,
		Circle,
		Square,
		Triangle,
		L1,
		R1,
		L2,
		R2,
		Options,
		Touchpad,
		DpadLeft,
		DpadRight,
		DpadUp,
		Dpad,
	};

	enum class Page
	{
		Closed,
		Main,
		HowToPlay,
		Options,
		Quit,
		About,
		GameDialog, // the game's controls dialog or high scores, over the paused table
	};

	Page page = Page::Closed;
	Page shownPage = Page::Closed; // what was drawn last frame: a new page takes the pad's focus
	bool pausedByMenu = false;
	int dialogFrames = 0;
	bool openRequested = false;
	const char* hint = nullptr; // the focused row's explanation, shown above the prompt bar

	// --- /data/SpaceCadetPinball/ps4.cfg ---------------------------------------------------------
	struct Ps4Settings
	{
		int resolution = 0; // index in RESOLUTIONS
		int fps = 60;
		bool showFps = false;
		bool seenHelp = false;
	} cfg;

	struct Resolution
	{
		int w, h;
		const char* name;
	};
	const Resolution RESOLUTIONS[] = {
		{1920, 1080, "1080p"},
		{1280, 720, "720p"},
	};
	constexpr int RESOLUTION_COUNT = sizeof RESOLUTIONS / sizeof RESOLUTIONS[0];
	const int FPS_CHOICES[] = {30, 60};
	const int UPS_CHOICES[] = {60, 120, 240};

	const char* CFG_PATH = PS4_DATA_DIR "ps4.cfg";

	void LoadCfg()
	{
		FILE* f = fopen(CFG_PATH, "r");
		if (!f)
			return;
		char line[128];
		while (fgets(line, sizeof line, f))
		{
			char key[64];
			int value;
			if (sscanf(line, "%63[^=]=%d", key, &value) != 2)
				continue;
			if (!strcmp(key, "resolution"))
				cfg.resolution = value;
			else if (!strcmp(key, "fps"))
				cfg.fps = value;
			else if (!strcmp(key, "show_fps"))
				cfg.showFps = value != 0;
			else if (!strcmp(key, "seen_help"))
				cfg.seenHelp = value != 0;
		}
		fclose(f);
		if (cfg.resolution < 0 || cfg.resolution >= RESOLUTION_COUNT || !ps4_video_supports(RESOLUTIONS[cfg.resolution].w))
			cfg.resolution = 0;
		if (cfg.fps != 30)
			cfg.fps = 60;
	}

	void SaveCfg()
	{
		FILE* f = fopen(CFG_PATH, "w");
		if (!f)
			return;
		fprintf(f, "resolution=%d\nfps=%d\nshow_fps=%d\nseen_help=%d\n", cfg.resolution, cfg.fps, cfg.showFps ? 1 : 0,
		        cfg.seenHelp ? 1 : 0);
		fclose(f);
	}

	// --- sizes -----------------------------------------------------------------------------------
	// 1 at 1080p with the default text size; everything is laid out in 1080p pixels times this.
	float Scale()
	{
		return ImGui::GetIO().DisplaySize.y / 1080.0f * (options::Options.UIScale / 2.0f);
	}

	float ScreenScale()
	{
		return ImGui::GetIO().DisplaySize.y / 1080.0f;
	}

	ImFont* Or(ImFont* font)
	{
		return font ? font : ImGui::GetFont();
	}

	// Text in a font at a 1080p size.
	void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text)
	{
		font = Or(font);
		dl->AddText(font, size * Scale(), pos, col, text);
	}

	ImVec2 TextSize(ImFont* font, float size, const char* text)
	{
		return Or(font)->CalcTextSizeA(size * Scale(), FLT_MAX, 0, text);
	}

	ImU32 Alpha(ImU32 col, float a)
	{
		const int alpha = static_cast<int>(((col >> IM_COL32_A_SHIFT) & 0xFF) * a);
		return (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
	}

	// --- shading ---------------------------------------------------------------------------------
	// Large translucent areas (the dimmed table, the panel, the prompt bar, cards) are NOT ImGui
	// rectangles: SDL's software renderer blends those as triangles, pixel by pixel, and four
	// full-screen layers made the menu crawl (v00.04). They are recorded here and applied to the
	// frame's pixels in one integer pass (ShadeScreen) after the table is drawn and before ImGui draws:
	// each pixel is shifted right (halved per step) and a tint is added.
	struct Shade
	{
		int x0, y0, x1, y1;
		int shift;   // 1 = 50%, 2 = 25%, 3 = 12.5% of the picture left
		Uint32 tint; // added after, 0x00RRGGBB
	};
	std::vector<Shade> shades;

	void AddShade(float x0, float y0, float x1, float y1, int shift, Uint32 tint = 0)
	{
		shades.push_back({static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1), static_cast<int>(y1), shift,
		                  tint});
	}

	const Uint32 NAVY = 0x00050A1A;

	// --- PlayStation button icons ----------------------------------------------------------------
	// A button at centre c, r = radius in screen pixels.
	void Icon(ImDrawList* dl, ImVec2 c, float r, Btn b)
	{
		const float t = r * 0.16f; // stroke
		auto disc = [&]
		{
			dl->AddCircleFilled(c, r, IM_COL32(28, 32, 46, 255), 32);
			dl->AddCircle(c, r, IM_COL32(80, 88, 110, 255), 32, r * 0.08f);
		};
		auto pill = [&](const char* label, float widthR)
		{
			const ImVec2 a(c.x - r * widthR, c.y - r * 0.78f), z(c.x + r * widthR, c.y + r * 0.78f);
			dl->AddRectFilled(a, z, IM_COL32(28, 32, 46, 255), r * 0.45f);
			dl->AddRect(a, z, IM_COL32(80, 88, 110, 255), r * 0.45f, 0, r * 0.08f);
			ImFont* f = Or(fontSmall);
			const float size = r * 0.95f;
			const ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0, label);
			dl->AddText(f, size, ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), TEXT, label);
		};
		auto dpad = [&](int lit) // 0 none, 1 left, 2 right, 3 up
		{
			const float a = r * 0.3f, l = r * 0.95f;
			const ImU32 base = IM_COL32(120, 128, 150, 255), on = ACCENT;
			dl->AddRectFilled(ImVec2(c.x - l, c.y - a), ImVec2(c.x - a, c.y + a), lit == 1 ? on : base, a * 0.4f);
			dl->AddRectFilled(ImVec2(c.x + a, c.y - a), ImVec2(c.x + l, c.y + a), lit == 2 ? on : base, a * 0.4f);
			dl->AddRectFilled(ImVec2(c.x - a, c.y - l), ImVec2(c.x + a, c.y - a), lit == 3 ? on : base, a * 0.4f);
			dl->AddRectFilled(ImVec2(c.x - a, c.y + a), ImVec2(c.x + a, c.y + l), base, a * 0.4f);
			dl->AddRectFilled(ImVec2(c.x - a, c.y - a), ImVec2(c.x + a, c.y + a), base);
		};
		switch (b)
		{
		case Btn::Cross:
		{
			disc();
			const float k = r * 0.42f;
			const ImU32 col = IM_COL32(124, 178, 232, 255);
			dl->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), col, t);
			dl->AddLine(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), col, t);
			break;
		}
		case Btn::Circle:
			disc();
			dl->AddCircle(c, r * 0.46f, IM_COL32(255, 102, 102, 255), 24, t);
			break;
		case Btn::Square:
		{
			disc();
			const float k = r * 0.4f;
			dl->AddRect(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), IM_COL32(255, 105, 248, 255), 0, 0, t);
			break;
		}
		case Btn::Triangle:
		{
			disc();
			const float k = r * 0.5f;
			dl->AddTriangle(ImVec2(c.x, c.y - k), ImVec2(c.x + k * 0.9f, c.y + k * 0.6f),
			                ImVec2(c.x - k * 0.9f, c.y + k * 0.6f), IM_COL32(64, 226, 160, 255), t);
			break;
		}
		case Btn::L1: pill("L1", 1.25f);
			break;
		case Btn::R1: pill("R1", 1.25f);
			break;
		case Btn::L2: pill("L2", 1.25f);
			break;
		case Btn::R2: pill("R2", 1.25f);
			break;
		case Btn::Options: pill("OPTIONS", 2.6f);
			break;
		case Btn::Touchpad: pill("TOUCH PAD", 3.2f);
			break;
		case Btn::DpadLeft: dpad(1);
			break;
		case Btn::DpadRight: dpad(2);
			break;
		case Btn::DpadUp: dpad(3);
			break;
		case Btn::Dpad: dpad(0);
			break;
		}
	}

	// How wide an icon is, in radii.
	float IconWidth(Btn b)
	{
		switch (b)
		{
		case Btn::L1:
		case Btn::R1:
		case Btn::L2:
		case Btn::R2: return 2.5f;
		case Btn::Options: return 5.2f;
		case Btn::Touchpad: return 6.4f;
		default: return 2.0f;
		}
	}

	struct Prompt
	{
		Btn button;
		const char* label;
	};

	// The bar of button prompts along the bottom of the screen, right-aligned.
	void PromptBar(std::initializer_list<Prompt> prompts)
	{
		const ImVec2 screen = ImGui::GetIO().DisplaySize;
		const float s = Scale();
		ImDrawList* dl = ImGui::GetForegroundDrawList();
		const float barH = 76 * s, y = screen.y - barH;
		AddShade(0, y, screen.x, screen.y, 1); // half again, over whatever is under it
		dl->AddLine(ImVec2(0, y), ImVec2(screen.x, y), Alpha(ACCENT, 0.25f), 1.5f * s);
		const float r = 17 * s, gap = 44 * s;
		float x = screen.x - 64 * s;
		const float cy = y + barH / 2;
		// from the right
		std::vector<Prompt> list(prompts);
		for (auto it = list.rbegin(); it != list.rend(); ++it)
		{
			const ImVec2 ts = TextSize(fontSmall, 26, it->label);
			x -= ts.x;
			Text(dl, fontSmall, 26, ImVec2(x, cy - ts.y / 2), TEXT, it->label);
			x -= 12 * s + IconWidth(it->button) * r / 2;
			Icon(dl, ImVec2(x, cy), r, it->button);
			x -= IconWidth(it->button) * r / 2 + gap;
		}
	}

	// The dimmed table, the left panel, the title and the page name.
	void Backdrop(const char* pageName, float panelWidth)
	{
		const ImVec2 screen = ImGui::GetIO().DisplaySize;
		const float s = Scale();
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		// the panel (12.5% of the table, navy), a stepped fade out of it, the rest of the table at 50%
		const float w = panelWidth * screen.x, fade = 220 * s;
		AddShade(0, 0, w, screen.y, 3, NAVY);
		const int steps[] = {3, 2, 2, 1};
		for (int i = 0; i < 4; i++)
			AddShade(w + fade * i / 4, 0, w + fade * (i + 1) / 4, screen.y, steps[i], steps[i] >= 2 ? NAVY >> 1 : 0);
		AddShade(w + fade, 0, screen.x, screen.y, 1);
		// the title, with one dark shadow (an 8-pass glow cost too much in software)
		const float x = 0.065f * screen.x, y = 0.09f * screen.y;
		Text(dl, fontTitle, 76, ImVec2(x + 3 * s, y + 4 * s), IM_COL32(0, 0, 0, 255), "SPACE CADET");
		Text(dl, fontTitle, 76, ImVec2(x, y), ACCENT, "SPACE CADET");
		const float titleH = TextSize(fontTitle, 76, "SPACE CADET").y;
		Text(dl, fontHeader, 28, ImVec2(x + 4 * s, y + titleH + 4 * s), ACCENT2, "3D PINBALL");
		// page name with a rule
		const float py = y + titleH + 70 * s;
		Text(dl, fontHeader, 30, ImVec2(x, py), TEXT, pageName);
		const float ny = py + TextSize(fontHeader, 30, pageName).y + 12 * s;
		dl->AddRectFilled(ImVec2(x, ny), ImVec2(x + w * 0.62f, ny + 2 * s), ACCENT);
	}

	// The focused row's explanation, above the prompt bar.
	void HintLine()
	{
		if (!hint)
			return;
		const ImVec2 screen = ImGui::GetIO().DisplaySize;
		const float s = Scale();
		Text(ImGui::GetForegroundDrawList(), fontBody, 28, ImVec2(0.065f * screen.x, screen.y - 76 * s - 58 * s),
		     TEXT_DIM, hint);
	}

	// --- applying settings -----------------------------------------------------------------------
	void ApplyTextScale()
	{
		// the fonts are made at their 1080p sizes; UI Scale 2 (the PS4 default) is 100%
		winmain::ImIO->FontGlobalScale = ScreenScale() * options::Options.UIScale / 2.0f;
	}

	void ApplyResolution()
	{
		const Resolution& r = RESOLUTIONS[cfg.resolution];
		int w = 0, h = 0;
		SDL_GetWindowSize(winmain::MainWindow, &w, &h);
		if (w != r.w || h != r.h)
		{
			SDL_SetWindowSize(winmain::MainWindow, r.w, r.h);
			fullscrn::window_size_changed();
		}
		winmain::ImIO->FontGlobalScale = r.h / 1080.0f * options::Options.UIScale / 2.0f;
		printf("PS4: screen %dx%d\n", r.w, r.h);
	}

	void ApplyFrameRate()
	{
		auto& o = options::Options;
		o.FramesPerSecond.V = cfg.fps; // 30 is below the game's own minimum: kept in ps4.cfg, set here
		if (o.UpdatesPerSecond.V < cfg.fps)
			o.UpdatesPerSecond.V = cfg.fps;
		winmain::UpdateFrameRate();
	}

	// --- opening and closing ---------------------------------------------------------------------
	void Open(Page p)
	{
		if (page == Page::Closed && !winmain::single_step)
		{
			winmain::pause(false);
			pausedByMenu = true;
		}
		page = p;
	}

	void Close()
	{
		page = Page::Closed;
		ImGui::FocusWindow(nullptr);
		if (pausedByMenu)
		{
			pausedByMenu = false;
			winmain::end_pause();
		}
	}

	// The game's own dialog (controls, high scores) over the paused table; the menu comes back after it.
	void OpenGameDialog()
	{
		page = Page::GameDialog;
		dialogFrames = 0;
	}

	// --- rows ------------------------------------------------------------------------------------
	bool PadPressed(ImGuiKey key)
	{
		return ImGui::IsKeyPressed(key, true);
	}

	// The window the rows go in: transparent, at x/y (fractions of the screen), w wide; h = 0 fits
	// the rows, else a scrolling list that high. Takes the pad's focus when the page is new.
	void BeginRows(const char* id, float x, float y, float w, float h)
	{
		const ImVec2 screen = ImGui::GetIO().DisplaySize;
		ImGui::SetNextWindowPos(ImVec2(x * screen.x, y * screen.y));
		ImGui::SetNextWindowSize(ImVec2(w * screen.x, h * screen.y));
		if (shownPage != page)
			ImGui::SetNextWindowFocus();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6 * Scale()));
		ImGui::PushStyleColor(ImGuiCol_Header, 0);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
		ImGui::PushStyleColor(ImGuiCol_NavHighlight, 0);
		ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
		             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		if (shownPage != page)
		{
			ImGuiContext& g = *GImGui;
			g.NavInputSource = ImGuiInputSource_Gamepad;
			g.NavDisableHighlight = false;
			g.NavDisableMouseHover = true;
		}
		hint = nullptr;
	}

	void EndRows()
	{
		ImGui::End();
		ImGui::PopStyleColor(4);
		ImGui::PopStyleVar(2);
	}

	void FirstItemFocus()
	{
		if (ImGui::IsWindowAppearing())
			ImGui::SetItemDefaultFocus();
	}

	bool Back()
	{
		return ImGui::IsWindowFocused() && PadPressed(ImGuiKey_GamepadFaceRight);
	}

	// One row: a full-width Selectable, its highlight drawn here. Returns {picked, focused}.
	struct RowState
	{
		bool picked, focused;
		ImVec2 a, z;
	};

	RowState RowBase(const char* id, float height, bool enabled, const char* help)
	{
		const float s = Scale();
		RowState st{};
		if (!enabled)
			ImGui::BeginDisabled();
		st.picked = ImGui::Selectable(id, false, ImGuiSelectableFlags_None, ImVec2(0, height * s));
		if (!enabled)
			ImGui::EndDisabled();
		st.picked = st.picked && enabled;
		st.focused = ImGui::IsItemFocused();
		st.a = ImGui::GetItemRectMin();
		st.z = ImGui::GetItemRectMax();
		if (st.focused)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilledMultiColor(st.a, st.z, Alpha(ACCENT, 0.32f), Alpha(ACCENT, 0.0f), Alpha(ACCENT, 0.0f),
			                            Alpha(ACCENT, 0.32f));
			dl->AddRectFilled(st.a, ImVec2(st.a.x + 6 * s, st.z.y), ACCENT);
			if (help)
				hint = help;
		}
		return st;
	}

	// A menu entry: Cross picks it.
	bool Item(const char* label, bool enabled = true, const char* help = nullptr)
	{
		ImGui::PushID(label);
		const RowState st = RowBase("##item", 60, enabled, help);
		ImGui::PopID();
		const float s = Scale();
		const ImVec2 ts = TextSize(fontItem, 40, label);
		Text(ImGui::GetWindowDrawList(), fontItem, 40,
		     ImVec2(st.a.x + (st.focused ? 34 : 26) * s, (st.a.y + st.z.y - ts.y) / 2),
		     !enabled ? TEXT_OFF : st.focused ? TEXT : TEXT_DIM, label);
		return st.picked;
	}

	// A setting: label on the left, value on the right between arrows. Cross or right steps forward,
	// left steps back. Returns -1, 0 or +1.
	int ValueRow(const char* label, const char* value, const char* help = nullptr)
	{
		ImGui::PushID(label);
		const RowState st = RowBase("##value", 56, true, help);
		ImGui::PopID();
		const float s = Scale();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 ls = TextSize(fontItem, 32, label);
		const float cy = (st.a.y + st.z.y) / 2;
		Text(dl, fontItem, 32, ImVec2(st.a.x + 30 * s, cy - ls.y / 2), st.focused ? TEXT : TEXT_DIM, label);
		const ImVec2 vs = TextSize(fontItem, 32, value);
		const float right = st.z.x - 28 * s, arrow = 10 * s;
		const float vx = right - arrow * 2 - 14 * s - vs.x;
		Text(dl, fontItem, 32, ImVec2(vx, cy - vs.y / 2), st.focused ? ACCENT : TEXT, value);
		if (st.focused)
		{
			const float lx = vx - 14 * s - arrow * 2;
			dl->AddTriangleFilled(ImVec2(lx, cy), ImVec2(lx + arrow * 1.6f, cy - arrow), ImVec2(lx + arrow * 1.6f, cy + arrow), ACCENT);
			dl->AddTriangleFilled(ImVec2(right, cy), ImVec2(right - arrow * 1.6f, cy - arrow), ImVec2(right - arrow * 1.6f, cy + arrow), ACCENT);
		}
		if (!st.focused)
			return st.picked ? 1 : 0;
		if (PadPressed(ImGuiKey_GamepadDpadLeft) || PadPressed(ImGuiKey_GamepadLStickLeft))
			return -1;
		if (st.picked || PadPressed(ImGuiKey_GamepadDpadRight) || PadPressed(ImGuiKey_GamepadLStickRight))
			return 1;
		return 0;
	}

	bool OnOffRow(const char* label, bool on, const char* help = nullptr)
	{
		return ValueRow(label, on ? "On" : "Off", help) != 0;
	}

	// A section title in the options list (not focusable).
	void Section(const char* title)
	{
		const float s = Scale();
		ImGui::Dummy(ImVec2(0, 14 * s));
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const ImVec2 ts = TextSize(fontHeader, 24, title);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		Text(dl, fontHeader, 24, ImVec2(p.x + 30 * s, p.y), ACCENT2, title);
		const float ly = p.y + ts.y / 2;
		dl->AddLine(ImVec2(p.x + 30 * s + ts.x + 16 * s, ly), ImVec2(p.x + ImGui::GetContentRegionAvail().x - 28 * s, ly),
		            Alpha(ACCENT2, 0.35f), 1.5f * s);
		ImGui::Dummy(ImVec2(0, ts.y + 6 * s));
	}

	int Step(int value, int dir, int lo, int hi, int step)
	{
		value += dir * step;
		return value < lo ? lo : value > hi ? hi : value;
	}

	template <size_t N>
	int StepChoice(const int (&choices)[N], int current, int dir)
	{
		size_t i = 0;
		while (i < N && choices[i] < current)
			i++;
		if (i == N)
			i = N - 1;
		if (dir < 0 && i > 0)
			i--;
		else if (dir > 0 && choices[i] == current && i + 1 < N)
			i++;
		return choices[i];
	}

	// --- pages -----------------------------------------------------------------------------------
	void MainPage()
	{
		Backdrop("PAUSED", 0.40f);
		BeginRows("##ps4main", 0.05f, 0.34f, 0.27f, 0);
		if (Item("Resume", true, "Back to the table."))
			Close();
		FirstItemFocus();
		if (Item("New Game", true, "Start over from the first ball."))
		{
			pausedByMenu = false; // new_game() ends the pause itself
			Close();
			winmain::new_game();
		}
		if (Item("Launch Ball", winmain::LaunchBallEnabled, "Put the next ball on the plunger."))
		{
			pausedByMenu = false;
			Close();
			winmain::end_pause();
			pb::launch_ball();
		}
		ImGui::Dummy(ImVec2(0, 14 * Scale()));
		if (Item("How to Play", true, "The rules, and every button."))
			page = Page::HowToPlay;
		if (Item("Options", true, "Sound, music, screen resolution, frame rate, buttons."))
			page = Page::Options;
		if (Item("High Scores", winmain::HighScoresEnabled, "The best five games on this table."))
		{
			pb::high_scores();
			OpenGameDialog();
		}
		if (Item("About", true, "Version, credits, and where to find the port."))
			page = Page::About;
		ImGui::Dummy(ImVec2(0, 14 * Scale()));
		if (Item("Quit Game", true, "Close Space Cadet Pinball."))
			page = Page::Quit;
		if (Back())
			Close();
		EndRows();
		HintLine();
		PromptBar({{Btn::Cross, "Select"}, {Btn::Circle, "Resume"}, {Btn::Options, "Resume"}});
	}

	void HowToPlayPage()
	{
		const ImVec2 screen = ImGui::GetIO().DisplaySize;
		const float s = Scale();
		Backdrop("HOW TO PLAY", 0.40f);
		ImDrawList* dl = ImGui::GetForegroundDrawList();

		// the rules, numbered, on the left
		struct Step
		{
			const char* title;
			const char* text;
		};
		const Step steps[] = {
			{"Launch", "The ball waits on the plunger in the lane on the right. Hold Cross to pull it back, "
			 "let go to shoot: the longer you hold, the harder the shot."},
			{"Flip", "Keep the ball in play with the flippers: L1 or L2 for the left one, R1 or R2 for the right."},
			{"Score", "Hit bumpers, targets and ramps. Mission targets light up missions, shown in the "
			 "panel on the right of the table."},
			{"Survive", "You have three balls. Nudge the table with the d-pad to save one, but nudge too "
			 "often and it tilts: the flippers die until the ball drains."},
		};
		float x = 0.065f * screen.x, y = 0.31f * screen.y;
		const float textW = 0.33f * screen.x;
		int n = 1;
		for (const Step& st : steps)
		{
			const float r = 22 * s;
			const ImVec2 c(x + r, y + r);
			dl->AddCircleFilled(c, r, Alpha(ACCENT, 0.18f), 32);
			dl->AddCircle(c, r, ACCENT, 32, 2 * s);
			char num[4];
			snprintf(num, sizeof num, "%d", n++);
			const ImVec2 ns = TextSize(fontHeader, 24, num);
			Text(dl, fontHeader, 24, ImVec2(c.x - ns.x / 2, c.y - ns.y / 2), ACCENT, num);
			Text(dl, fontItem, 32, ImVec2(x + 2 * r + 18 * s, y - 2 * s), TEXT, st.title);
			const float size = 26 * s;
			ImFont* f = Or(fontBody);
			const float ty = y + 40 * s;
			dl->AddText(f, size, ImVec2(x + 2 * r + 18 * s, ty), TEXT_DIM, st.text, nullptr, textW);
			const ImVec2 bs = f->CalcTextSizeA(size, FLT_MAX, textW, st.text);
			y = ty + bs.y + 22 * s;
		}

		// the buttons, on a card on the right
		const ImVec2 ca(0.56f * screen.x, 0.20f * screen.y), cz(0.94f * screen.x, 0.84f * screen.y);
		AddShade(ca.x, ca.y, cz.x, cz.y, 2, NAVY); // the card: shaded, square, with a plain outline
		dl->AddRect(ca, cz, Alpha(ACCENT, 0.35f), 0, 0, 1.5f * s);
		Text(dl, fontHeader, 28, ImVec2(ca.x + 40 * s, ca.y + 32 * s), ACCENT2, "CONTROLS");
		struct Row
		{
			Btn a, b;
			bool two;
			const char* text;
		};
		const Row rows[] = {
			{Btn::L1, Btn::L2, true, "Left flipper"},
			{Btn::R1, Btn::R2, true, "Right flipper"},
			{Btn::Cross, Btn::Cross, false, "Plunger: hold, then release"},
			{Btn::DpadLeft, Btn::DpadRight, true, "Nudge the table left / right"},
			{Btn::DpadUp, Btn::DpadUp, false, "Nudge the table up"},
			{Btn::Triangle, Btn::Triangle, false, "New game"},
			{Btn::Options, Btn::Touchpad, true, "Pause menu"},
		};
		float ry = ca.y + 110 * s;
		const float r = 20 * s, iconX = ca.x + 60 * s;
		for (const Row& row : rows)
		{
			float ix = iconX + IconWidth(row.a) * r / 2;
			Icon(dl, ImVec2(ix, ry), r, row.a);
			if (row.two)
			{
				ix += IconWidth(row.a) * r / 2 + 12 * s;
				Text(dl, fontSmall, 24, ImVec2(ix, ry - 14 * s), TEXT_DIM, "/");
				ix += 22 * s + IconWidth(row.b) * r / 2;
				Icon(dl, ImVec2(ix, ry), r, row.b);
			}
			const ImVec2 ts = TextSize(fontBody, 28, row.text);
			Text(dl, fontBody, 28, ImVec2(ca.x + 0.165f * screen.x, ry - ts.y / 2), TEXT, row.text);
			ry += 62 * s;
		}
		const char* note = "Every button can be changed in Options > Button Assignment.";
		Text(dl, fontBody, 24, ImVec2(ca.x + 40 * s, cz.y - 60 * s), TEXT_DIM, note);

		// one row under the card to take the pad: Cross or Circle goes back
		BeginRows("##ps4help", 0.56f, 0.855f, 0.16f, 0);
		if (Item("Got it"))
			page = Page::Main;
		FirstItemFocus();
		if (Back())
			page = Page::Main;
		EndRows();
		PromptBar({{Btn::Cross, "Got it"}, {Btn::Circle, "Back"}});
	}

	void OptionsPage()
	{
		auto& o = options::Options;
		char value[48];
		Backdrop("OPTIONS", 0.52f);
		BeginRows("##ps4options", 0.05f, 0.31f, 0.42f, 0.53f);

		Section("AUDIO");
		if (OnOffRow("Sound Effects", o.Sounds, "The table's sounds."))
			options::toggle(Menu1::Sounds);
		FirstItemFocus();
		snprintf(value, sizeof value, "%d%%", o.SoundVolume.V * 100 / options::MaxVolume);
		if (int d = ValueRow("Sound Volume", value))
		{
			o.SoundVolume.V = Step(o.SoundVolume.V, d, options::MinVolume, options::MaxVolume, 8);
			Sound::SetVolume(o.SoundVolume);
		}
		if (OnOffRow("Music", o.Music, "The table's MIDI music."))
			options::toggle(Menu1::Music);
		snprintf(value, sizeof value, "%d%%", o.MusicVolume.V * 100 / options::MaxVolume);
		if (int d = ValueRow("Music Volume", value))
		{
			o.MusicVolume.V = Step(o.MusicVolume.V, d, options::MinVolume, options::MaxVolume, 8);
			midi::SetVolume(o.MusicVolume);
		}

		Section("DISPLAY");
		if (int d = ValueRow("Resolution", RESOLUTIONS[cfg.resolution].name,
		                     "720p draws 2.25x fewer pixels: smoother, a little softer."))
		{
			int r = cfg.resolution;
			do
				r = (r + d + RESOLUTION_COUNT) % RESOLUTION_COUNT;
			while (r != cfg.resolution && !ps4_video_supports(RESOLUTIONS[r].w));
			if (r != cfg.resolution)
			{
				cfg.resolution = r;
				ApplyResolution();
				SaveCfg();
			}
		}
		snprintf(value, sizeof value, "%d fps", cfg.fps);
		if (int d = ValueRow("Frame Rate", value, "Pictures per second. 60 is smoothest."))
		{
			cfg.fps = StepChoice(FPS_CHOICES, cfg.fps, d);
			ApplyFrameRate();
			SaveCfg();
		}
		if (OnOffRow("Smooth Scaling", o.LinearFiltering, "Softer picture; costs speed. Off is fastest."))
			options::toggle(Menu1::WindowLinearFilter);
		if (OnOffRow("Sharp Pixels", o.IntegerScaling, "Whole-pixel scaling: crisp, with black borders."))
			options::toggle(Menu1::WindowIntegerScale);
		if (OnOffRow("Keep Aspect Ratio", o.UniformScaling, "Off stretches the table to the whole screen."))
			options::toggle(Menu1::WindowUniformScale);
		if (fullscrn::GetMaxResolution() > 0)
		{
			const int tableRes = o.Resolution.V < 0 ? fullscrn::GetMaxResolution() : o.Resolution.V;
			auto& res = fullscrn::resolution_array[tableRes];
			snprintf(value, sizeof value, "%d x %d", res.ScreenWidth, res.ScreenHeight);
			if (int d = ValueRow("Table Detail", value, "Full Tilt's table art size."))
			{
				const int next = Step(tableRes, d, 0, fullscrn::GetMaxResolution(), 1);
				options::toggle(static_cast<Menu1>(static_cast<int>(Menu1::R640x480) + next));
			}
		}
		snprintf(value, sizeof value, "%d%%", static_cast<int>(o.UIScale.V * 50.0f + 0.5f));
		if (int d = ValueRow("Menu Text Size", value))
		{
			o.UIScale.V = std::max(1.5f, std::min(3.0f, o.UIScale.V + d * 0.1f));
			ApplyTextScale();
		}
		if (OnOffRow("Show FPS", cfg.showFps, "A frame counter in the top-left corner."))
		{
			cfg.showFps = !cfg.showFps;
			SaveCfg();
		}

		Section("GAMEPLAY");
		snprintf(value, sizeof value, "%d", o.Players.V);
		if (int d = ValueRow("Players", value, "Players take turns. Changing it starts a new game."))
		{
			const int players = Step(o.Players.V, d, 1, 4, 1);
			if (players != o.Players.V)
			{
				const Menu1 ids[] = {Menu1::OnePlayer, Menu1::TwoPlayers, Menu1::ThreePlayers, Menu1::FourPlayers};
				options::toggle(ids[players - 1]);
				winmain::new_game();
				// new_game() ended the pause: the menu is still up, so pause again
				winmain::pause(false);
				pausedByMenu = true;
			}
		}
		snprintf(value, sizeof value, "%d / s", o.UpdatesPerSecond.V);
		if (int d = ValueRow("Physics Rate", value, "Ball physics steps per second. 120 is the default."))
		{
			o.UpdatesPerSecond.V = StepChoice(UPS_CHOICES, o.UpdatesPerSecond.V, d);
			ApplyFrameRate();
		}
		if (Item("Button Assignment", true, "Change which button does what."))
		{
			options::ShowControlDialog();
			OpenGameDialog();
		}
		ImGui::Dummy(ImVec2(0, 10 * Scale()));
		if (Back())
			page = Page::Main;
		EndRows();
		HintLine();
		PromptBar({{Btn::Dpad, "Change"}, {Btn::Cross, "Select"}, {Btn::Circle, "Back"}});
	}

	void AboutPage()
	{
		const ImVec2 screen = ImGui::GetIO().DisplaySize;
		const float s = Scale();
		Backdrop("ABOUT", 0.40f);
		ImDrawList* dl = ImGui::GetForegroundDrawList();

		// the port, on the left
		const float x = 0.065f * screen.x;
		float y = 0.32f * screen.y;
		Text(dl, fontItem, 40, ImVec2(x, y), TEXT, "Space Cadet Pinball for PS4");
		y += 54 * s;
		Text(dl, fontBody, 28, ImVec2(x, y), TEXT_DIM, "Version " PINBALL_PS4_VERSION);
		y += 70 * s;
		Text(dl, fontHeader, 24, ImVec2(x, y), ACCENT2, "PS4 PORT BY");
		y += 38 * s;
		Text(dl, fontTitle, 52, ImVec2(x + 3 * s, y + 3 * s), IM_COL32(0, 0, 0, 255), "iHaiDeeZ");
		Text(dl, fontTitle, 52, ImVec2(x, y), ACCENT, "iHaiDeeZ");
		y += TextSize(fontTitle, 52, "iHaiDeeZ").y + 26 * s;
		// the GitHub link in a rounded outline
		const char* link = "github.com/iHaiDeeZ";
		const ImVec2 ls = TextSize(fontItem, 32, link);
		const ImVec2 pa(x, y), pz(x + ls.x + 44 * s, y + ls.y + 20 * s);
		dl->AddRect(pa, pz, Alpha(ACCENT, 0.6f), (pz.y - pa.y) / 2, 0, 2 * s);
		Text(dl, fontItem, 32, ImVec2(x + 22 * s, y + 10 * s), TEXT, link);
		y = pz.y + 40 * s;
		ImFont* f = Or(fontBody);
		dl->AddText(f, 26 * s, ImVec2(x, y), TEXT_DIM,
		            "Source, updates and other PS4 ports on GitHub. Bugs and ideas are welcome there as issues.",
		            nullptr, 0.30f * screen.x);

		// credits, on a card on the right
		const ImVec2 ca(0.56f * screen.x, 0.20f * screen.y), cz(0.94f * screen.x, 0.84f * screen.y);
		AddShade(ca.x, ca.y, cz.x, cz.y, 2, NAVY);
		dl->AddRect(ca, cz, Alpha(ACCENT, 0.35f), 0, 0, 1.5f * s);
		Text(dl, fontHeader, 28, ImVec2(ca.x + 40 * s, ca.y + 32 * s), ACCENT2, "CREDITS");
		struct Credit
		{
			const char* who;
			const char* what;
		};
		const Credit credits[] = {
			{"3D Pinball for Windows - Space Cadet", "Cinematronics, Maxis, Microsoft (1995)"},
			{"Decompilation", "k4zmu2a and contributors - github.com/k4zmu2a/SpaceCadetPinball"},
			{"PS4 port", "iHaiDeeZ - menus, pad, picture, sound and music on PS4"},
			{"Libraries", "SDL2 + SDL_mixer (zlib), Dear ImGui (MIT), OpenOrbis toolchain"},
			{"Music instruments", "freepats (GPL)"},
			{"Fonts", "Exo 2 and Orbitron (SIL Open Font License)"},
		};
		float cy = ca.y + 100 * s;
		const float wrap = cz.x - ca.x - 80 * s;
		for (const Credit& c : credits)
		{
			Text(dl, fontItem, 28, ImVec2(ca.x + 40 * s, cy), TEXT, c.who);
			cy += 36 * s;
			dl->AddText(f, 24 * s, ImVec2(ca.x + 40 * s, cy), TEXT_DIM, c.what, nullptr, wrap);
			cy += f->CalcTextSizeA(24 * s, FLT_MAX, wrap, c.what).y + 22 * s;
		}
		Text(dl, fontBody, 22, ImVec2(ca.x + 40 * s, cz.y - 50 * s), TEXT_OFF,
		     "The table's art, sounds and music are Microsoft's.");

		BeginRows("##ps4about", 0.56f, 0.855f, 0.16f, 0);
		if (Item("Back"))
			page = Page::Main;
		FirstItemFocus();
		if (Back())
			page = Page::Main;
		EndRows();
		PromptBar({{Btn::Cross, "Back"}, {Btn::Circle, "Back"}});
	}

	void QuitPage()
	{
		const ImVec2 screen = ImGui::GetIO().DisplaySize;
		const float s = Scale();
		Backdrop("QUIT GAME", 0.40f);
		Text(ImGui::GetForegroundDrawList(), fontBody, 32, ImVec2(0.065f * screen.x, 0.36f * screen.y), TEXT,
		     "Quit Space Cadet Pinball?");
		(void)s;
		BeginRows("##ps4quit", 0.05f, 0.43f, 0.27f, 0);
		if (Item("Keep Playing"))
			page = Page::Main;
		FirstItemFocus();
		if (Item("Quit"))
		{
			SDL_Event event{SDL_QUIT};
			SDL_PushEvent(&event);
			page = Page::Closed;
		}
		if (Back())
			page = Page::Main;
		EndRows();
		PromptBar({{Btn::Cross, "Select"}, {Btn::Circle, "Back"}});
	}

	// The game's controls dialog is a plain window: give it the pad. High scores is a popup (modal),
	// which takes the focus itself. Back to the menu once neither is open.
	void GameDialogPage()
	{
		ImGuiContext& g = *GImGui;
		dialogFrames++;
		AddShade(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
		if (options::ControlDialogOpen())
		{
			ImGuiWindow* w = ImGui::FindWindowByName(pb::get_rc_string(Msg::KEYMAPPER_Caption));
			if (w != nullptr && g.NavWindow != w && g.OpenPopupStack.Size == 0)
			{
				ImGui::FocusWindow(w);
				g.NavInputSource = ImGuiInputSource_Gamepad;
				g.NavDisableHighlight = false;
				ImGui::NavInitWindow(w, true);
			}
			PromptBar({{Btn::Cross, "Select / change"}, {Btn::Circle, "Back"}});
			return;
		}
		if (dialogFrames > 3 && g.OpenPopupStack.Size == 0)
			page = Page::Main;
		else
			PromptBar({{Btn::Cross, "Select"}, {Btn::Circle, "Back"}});
	}

	void DrawFps()
	{
		char text[32];
		snprintf(text, sizeof text, "%d FPS", ps4_video_fps());
		ImDrawList* dl = ImGui::GetForegroundDrawList();
		const float s = ScreenScale();
		const ImVec2 ts = Or(fontSmall)->CalcTextSizeA(24 * s, FLT_MAX, 0, text);
		dl->AddRectFilled(ImVec2(16 * s, 14 * s), ImVec2(16 * s + ts.x + 24 * s, 14 * s + ts.y + 10 * s),
		                  IM_COL32(0, 0, 0, 160), 8 * s);
		dl->AddText(Or(fontSmall), 24 * s, ImVec2(28 * s, 19 * s), IM_COL32(120, 255, 160, 255), text);
	}

	// The game's own dialogs (high scores, controls, exit) in the same colours.
	void ApplyStyle()
	{
		ImGuiStyle& st = ImGui::GetStyle();
		st.WindowRounding = 14;
		st.ChildRounding = 10;
		st.FrameRounding = 8;
		st.PopupRounding = 12;
		st.GrabRounding = 8;
		st.TabRounding = 8;
		st.WindowPadding = ImVec2(26, 22);
		st.FramePadding = ImVec2(14, 8);
		st.ItemSpacing = ImVec2(14, 10);
		st.WindowBorderSize = 1;
		st.PopupBorderSize = 1;
		st.WindowTitleAlign = ImVec2(0.5f, 0.5f);
		ImVec4* c = st.Colors;
		const ImVec4 accent(64 / 255.f, 196 / 255.f, 1.0f, 1.0f);
		c[ImGuiCol_Text] = ImVec4(0.93f, 0.95f, 0.98f, 1);
		c[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.48f, 0.56f, 1);
		c[ImGuiCol_WindowBg] = ImVec4(0.024f, 0.04f, 0.095f, 0.95f);
		c[ImGuiCol_PopupBg] = ImVec4(0.03f, 0.05f, 0.11f, 0.97f);
		c[ImGuiCol_Border] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
		c[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.08f, 0.17f, 1);
		c[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.12f, 0.25f, 1);
		c[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.12f, 0.22f, 1);
		c[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.18f, 0.32f, 1);
		c[ImGuiCol_FrameBgActive] = ImVec4(0.14f, 0.22f, 0.40f, 1);
		c[ImGuiCol_Button] = ImVec4(0.10f, 0.16f, 0.30f, 1);
		c[ImGuiCol_ButtonHovered] = ImVec4(accent.x * 0.6f, accent.y * 0.6f, accent.z * 0.6f, 1);
		c[ImGuiCol_ButtonActive] = accent;
		c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.25f);
		c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.40f);
		c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
		c[ImGuiCol_SliderGrab] = accent;
		c[ImGuiCol_SliderGrabActive] = ImVec4(0.75f, 0.45f, 1, 1);
		c[ImGuiCol_CheckMark] = accent;
		c[ImGuiCol_Separator] = ImVec4(accent.x, accent.y, accent.z, 0.25f);
		c[ImGuiCol_TableHeaderBg] = ImVec4(0.07f, 0.12f, 0.25f, 1);
		c[ImGuiCol_TableBorderStrong] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
		c[ImGuiCol_TableBorderLight] = ImVec4(accent.x, accent.y, accent.z, 0.15f);
		c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
		c[ImGuiCol_NavHighlight] = accent;
		// no full-screen dimming behind modal dialogs: a blended full-screen rectangle in the software
		// renderer costs a frame (the menu shades the table itself, ShadeScreen)
		c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0);
	}
}

void ps4ui::LoadFonts()
{
	ImGuiIO& io = ImGui::GetIO();
	ImFontAtlas* atlas = io.Fonts;
	// the language's characters for the text fonts (the game's own ranges)
	static ImVector<ImWchar> ranges;
	ranges.clear();
	translations::GetGlyphRange(&ranges);
	ImFontConfig config{};
	config.OversampleH = 2;
	config.OversampleV = 1;
	auto add = [&](const char* file, float size, const ImWchar* glyphs) -> ImFont*
	{
		const std::string path = std::string(PS4_APP_DIR "fonts/") + file;
		FILE* f = fopen(path.c_str(), "rb");
		if (!f)
		{
			printf("PS4 UI: no %s, ImGui's own font instead\n", path.c_str());
			return nullptr;
		}
		fclose(f);
		return atlas->AddFontFromFileTTF(path.c_str(), size, &config, glyphs);
	};
	// the first one added is ImGui's default font: the game's dialogs use it too
	fontBody = add("Exo2-Regular.ttf", 30, ranges.Data);
	if (fontBody == nullptr)
		return;
	fontItem = add("Exo2-SemiBold.ttf", 40, ranges.Data);
	fontSmall = add("Exo2-SemiBold.ttf", 26, ranges.Data);
	fontTitle = add("Orbitron-Bold.ttf", 76, atlas->GetGlyphRangesDefault());
	fontHeader = add("Orbitron-Bold.ttf", 30, atlas->GetGlyphRangesDefault());
	atlas->Build();
	printf("PS4 UI: fonts loaded (atlas %dx%d)\n", atlas->TexWidth, atlas->TexHeight);
}

void ps4ui::Init()
{
	LoadCfg();
	ApplyStyle();
	options::Options.ShowMenu.V = false; // the menu bar is the PC's: the PS4 has the pause menu
	options::Options.FullScreen.V = false; // the window is sized to the screen resolution instead
	ApplyResolution();
	ApplyFrameRate();
	if (!cfg.seenHelp)
	{
		// the first time: how to play, before anything else (opened on the first frame, once the
		// game has started)
		cfg.seenHelp = true;
		SaveCfg();
		openRequested = true;
	}
}

void ps4ui::MenuButtonPressed()
{
	switch (page)
	{
	case Page::Closed:
		Open(Page::Main);
		break;
	case Page::GameDialog:
		break; // the game's dialog has the pad until it closes
	default:
		Close();
		break;
	}
}

bool ps4ui::MenuActive()
{
	return page != Page::Closed;
}

void ps4ui::AfterNewFrame()
{
	if (openRequested)
	{
		openRequested = false;
		Open(Page::HowToPlay);
	}
}

void ps4ui::ShadeScreen()
{
	if (shades.empty())
		return;
	// the software renderer queues its draws: the table must be in the pixels before they are shaded
	SDL_RenderFlush(winmain::Renderer);
	SDL_Surface* surface = SDL_GetWindowSurface(winmain::MainWindow);
	if (surface == nullptr || surface->format->BytesPerPixel != 4)
		return;
	for (const Shade& sh : shades)
	{
		const int x0 = std::max(0, sh.x0), x1 = std::min(surface->w, sh.x1);
		const int y0 = std::max(0, sh.y0), y1 = std::min(surface->h, sh.y1);
		if (x0 >= x1 || y0 >= y1)
			continue;
		// each channel shifted right on its own: the mask drops the bits that crossed into the next one
		const Uint32 mask = (0xFFu >> sh.shift) * 0x01010101u;
		const int shift = sh.shift;
		const Uint32 tint = sh.tint;
		for (int y = y0; y < y1; y++)
		{
			Uint32* row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels) + y * surface->pitch);
			for (int x = x0; x < x1; x++)
				row[x] = ((row[x] >> shift) & mask) + tint;
		}
	}
}

void ps4ui::BeforeRender()
{
	ImGuiContext& g = *GImGui;
	shades.clear();
	if (page == Page::Closed)
	{
		// Nothing of ours open: no window keeps the pad from the table (the high score popup and the
		// controls dialog take it themselves when the game opens them).
		if (g.NavWindow != nullptr && g.OpenPopupStack.Size == 0 && !options::ControlDialogOpen())
			ImGui::FocusWindow(nullptr);
	}
	const Page drawing = page;
	switch (page)
	{
	case Page::Main: MainPage();
		break;
	case Page::HowToPlay: HowToPlayPage();
		break;
	case Page::Options: OptionsPage();
		break;
	case Page::About: AboutPage();
		break;
	case Page::Quit: QuitPage();
		break;
	case Page::GameDialog: GameDialogPage();
		break;
	default: break;
	}
	shownPage = drawing;
	if (cfg.showFps)
		DrawFps();
}

void ps4ui::ShowMessage(const char* title, const char* message)
{
	if (winmain::Renderer == nullptr || ImGui::GetCurrentContext() == nullptr)
		return; // too early to draw: the log has it
	// a loop of its own until Cross (or the PS button closes the app)
	bool crossWasUp = false;
	const Uint32 shownAt = SDL_GetTicks();
	for (;;)
	{
		SDL_Event event;
		bool done = false;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_JOYDEVICEADDED && SDL_IsGameController(event.jdevice.which))
				SDL_GameControllerOpen(event.jdevice.which);
			if (event.type == SDL_CONTROLLERBUTTONUP && event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
				crossWasUp = true;
			if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_A && crossWasUp)
				done = true;
			if (event.type == SDL_QUIT)
				done = true;
		}
		if (done)
			return;
		// a press from before the message (still held) doesn't close it: a fresh one after a second does
		if (!crossWasUp && SDL_GetTicks() - shownAt > 1000)
			crossWasUp = true;
		ImGui_ImplSDL2_NewFrame();
		ImGui_Render_NewFrame();
		ImGui::NewFrame();
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
		             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav);
		ImGui::TextUnformatted(message);
		ImGui::Separator();
		ImGui::TextUnformatted("Space Cadet Pinball for PS4 looks for the game's files in /app0/ (the package)\n"
			"and in " PS4_DATA_DIR " on the console (copy them there by FTP).\n\n"
			"Press Cross to close.");
		ImGui::End();
		ImGui::Render();
		SDL_SetRenderDrawColor(winmain::Renderer, 0, 0, 0, 255);
		SDL_RenderClear(winmain::Renderer);
		ImGui_Render_RenderDrawData(ImGui::GetDrawData());
		SDL_RenderPresent(winmain::Renderer);
		SDL_Delay(16);
	}
}
