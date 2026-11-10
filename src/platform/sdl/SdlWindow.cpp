#include "platform/sdl/SdlWindow.h"
#include "platform/sdl/WorldGpuRenderer.h"
#include "core/DiagnosticNotice.h"
#include "platform/sdl/SdlAudioDevice.h"
#include "platform/sdl/SdlWindowPlacement.h"
#include "platform/sdl/XeenMainScreenInput.h"
#include "platform/sdl/XeenRussianKeys.h"
#include "core/PresentationClock.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdint>
#include <array>
#include <deque>
#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <vector>
#include <limits>
#include <stdexcept>

namespace mmodern {
namespace {

// Maintainer-approved IBM enhanced-keyboard fallback (F3 2Bh): 500 ms,
// then (8 + 3) * 2 / 240 seconds. Independent of host OS repeat settings.
// Internal SDL queue marker: observe a cleared make without delivering its code.
constexpr Uint8 clearedKeyMake=2;
constexpr std::uint64_t navigationRepeatDelay = 500000000;
constexpr std::uint64_t navigationRepeatPeriod = (11000000000ULL + 119) / 120;
bool modernWorld = false;

// Host window presentation shared by the title and gameplay windows.
struct PlacementMemory {
	WindowPlacement placement, saved;
	std::optional<std::filesystem::path> file;
	bool loaded = false;
};
PlacementMemory &placementMemory() {
	static PlacementMemory memory;
	return memory;
}
void savePlacement() {
	auto &memory = placementMemory();
	if (!memory.file || memory.placement == memory.saved) return;
	if (writeWindowPlacement(*memory.file, memory.placement)) memory.saved = memory.placement;
	else std::cerr << "Cannot save window settings: " << memory.file->u8string() << '\n';
}
// Fullscreen, maximized and minimized sizes are not the windowed size.
void recordWindowedSize(SDL_Window *window) {
	if (SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)) return;
	int width = 0, height = 0;
	SDL_GetWindowSize(window, &width, &height);
	if (width > 0 && height > 0) {
		placementMemory().placement.width = width;
		placementMemory().placement.height = height;
	}
}

bool uploadFrame(WorldGpuRenderer &gpu, const IndexedFrame &frame,
		int expectedWidth, int expectedHeight, bool allowModern = true) {
	if (!frame.isValid() || frame.width != expectedWidth || frame.height != expectedHeight) {
		std::cerr << "Invalid indexed framebuffer or changed dimensions.\n";
		return false;
	}
	gpu.upload(frame, modernWorld, allowModern);
	return true;
}

SDL_Scancode physicalScan(const SDL_KeyboardEvent &key) {
    // A modifier/layout change can change keysym between make and break.
    // Synthetic clients may omit scancode; native SDL always supplies it.
    return key.keysym.scancode>SDL_SCANCODE_UNKNOWN && key.keysym.scancode<SDL_NUM_SCANCODES ?
        key.keysym.scancode:SDL_GetScancodeFromKey(key.keysym.sym);
}
// M110 Part C: the Russian xeen.dat keyboard of the running loop, or null.
const XeenKeyboard *russianKeyboard=nullptr;
// The CD's Shift list: the shifted symbol of a plain code.
unsigned shiftedSymbol(unsigned code,SDL_Keycode symbol) {
    if(code>='0' && code<='9')return unsigned(")!@#$%^&*("[code-'0']);
    switch(symbol) {
    case SDLK_EQUALS:return '+';
    case SDLK_LEFTBRACKET:return '{';case SDLK_RIGHTBRACKET:return '}';
    case SDLK_BACKSLASH:return '|';case SDLK_SEMICOLON:return ':';
    case SDLK_QUOTE:return '"';case SDLK_BACKQUOTE:return '~';
    case SDLK_COMMA:return '<';case SDLK_PERIOD:return '>';
    case SDLK_SLASH:return '?';
    default:return code; // Shift-minus stays minus in the DOS inverse lookup.
    }
}
// DOS Set-1 translation, documented in rules/world-events.md#event-dispatch.
// Translate the plain code before applying Ctrl (which overrides Shift).
// The program's code (zero for none), before InputKey symbols.
unsigned dosCode(const SDL_KeyboardEvent &key) {
    const auto symbol=key.keysym.sym;
    unsigned code=0;
    // Printable Russian keys by position, from the Russian table.
    if(russianKeyboard)if(const auto russian=xeenRussianKeyCode(*russianKeyboard,unsigned(physicalScan(key)),
        key.keysym.mod & KMOD_SHIFT,key.keysym.mod & KMOD_CTRL))return russian;
    if(symbol>=SDLK_a && symbol<=SDLK_z)code='A'+symbol-SDLK_a;
    else if(symbol>=SDLK_0 && symbol<=SDLK_9)code=unsigned(symbol);
    else if(symbol>=SDLK_F1 && symbol<=SDLK_F10)code=0xc9+symbol-SDLK_F1;
    else switch(symbol) {
    case SDLK_ESCAPE:code=27;break;
    case SDLK_RETURN:case SDLK_KP_ENTER:code=13;break;
    case SDLK_TAB:code=9;break;
    case SDLK_BACKSPACE:code=8;break;
    case SDLK_SPACE:code=32;break;
    case SDLK_LEFT:case SDLK_KP_4:code=0xf0;break;
    case SDLK_RIGHT:case SDLK_KP_6:code=0xf1;break;
    case SDLK_UP:case SDLK_KP_8:code=0xf2;break;
    case SDLK_DOWN:case SDLK_KP_2:code=0xf3;break;
    case SDLK_HOME:case SDLK_KP_7:code=0xdc;break;
    case SDLK_END:case SDLK_KP_1:code=0xdd;break;
    case SDLK_PAGEUP:case SDLK_KP_9:code=0xde;break;
    case SDLK_PAGEDOWN:case SDLK_KP_3:code=0xdf;break;
    case SDLK_INSERT:case SDLK_KP_0:code=0xe0;break;
    case SDLK_KP_5:code='5';break;
    case SDLK_CAPSLOCK:code=0xfc;break;
    case SDLK_NUMLOCKCLEAR:code=0xfb;break;
    case SDLK_SCROLLLOCK:code=0xfa;break;
    case SDLK_KP_PLUS:code='+';break;
    case SDLK_KP_MINUS:code='-';break;
    case SDLK_KP_MULTIPLY:code='*';break;
    case SDLK_KP_DIVIDE:code='/';break;
    case SDLK_KP_EQUALS:code='=';break; // Existing host alias; DOS hardware equivalence unknown.
    case SDLK_MINUS:case SDLK_EQUALS:case SDLK_LEFTBRACKET:case SDLK_RIGHTBRACKET:
    case SDLK_BACKSLASH:case SDLK_SEMICOLON:case SDLK_QUOTE:case SDLK_BACKQUOTE:
    case SDLK_COMMA:case SDLK_PERIOD:case SDLK_SLASH:code=unsigned(symbol);break;
    default:return 0; // Includes Delete, keypad decimal, F11/F12 and modifiers.
    }
    if(russianKeyboard) {
        // The Russian Ctrl codes are gone (Ctrl+Left/Right are plain turns);
        // Shift and Ctrl keep these codes, except keypad 5's listed '%'.
        if(!(key.keysym.mod & KMOD_SHIFT) && (key.keysym.mod & KMOD_CTRL) && symbol==SDLK_KP_5)return '%';
        return code;
    }
    if(key.keysym.mod & KMOD_CTRL) {if(code>=0x40)code-=0x40;}
    else if(key.keysym.mod & KMOD_SHIFT)code=shiftedSymbol(code,symbol);
    return code;
}
unsigned inputKey(const SDL_KeyboardEvent &key) {
    const auto code=dosCode(key);
    if(code==0xd2)return 0; // F10's DOS-prompt wrapper consumes it.
    if(code>=0xc9 && code<=0xd1)return InputKey::F1+code-0xc9;
    switch(code) {
    case 0xf0:return InputKey::Left;case 0xf1:return InputKey::Right;
    case 0xf2:return InputKey::Up;case 0xf3:return InputKey::Down;
    case 0xdc:return InputKey::Home;case 0xdd:return InputKey::End;
    case 0xde:return InputKey::PageUp;case 0xdf:return InputKey::PageDown;
    default:return code>='A' && code<='Z'?code+('a'-'A'):code;
    }
}
// The internal key of a Russian program key in this context's key set.
unsigned contextKey(unsigned code,const InputContext &context) {
    if(!russianKeyboard || !code)return code;
    if(context.dialog)return context.dialog->keySet?russianKeyboard->key(context.dialog->keySet,code):code;
    return russianKeyboard->key(context.mainScreen==MainScreen::Combat?"COMBAT":
        context.mainScreen==MainScreen::Exploration?"MAIN":"YESNO",code);
}
unsigned inputKey(const SDL_KeyboardEvent &key,const InputContext &context) {return contextKey(inputKey(key),context);}
std::optional<PlayerAction> playerAction(unsigned code, MainScreen screen) {
    if(!code)return {};
    const bool combat=screen==MainScreen::Combat;
    if(code>=InputKey::F1 && code<InputKey::F1+6)return xeenMainScreenMemberKey(code);
    switch(code) {
    case 'w':return WaitAction{};
    case 'b':return screen==MainScreen::Exploration?PlayerAction{BashAction{}}:PlayerAction{BlockAction{}};
    case 'c':return CastSpellAction{};
    case 'r':return combat?PlayerAction{RevisitCompletedAction{}}:PlayerAction{RestAction{}};
    case 's':if(!combat)return ShootAction{};break;
    case 'o':if(combat)return QuickFightOptionsAction{};break;
    case 'f':if(combat)return QuickFightAction{};break;
    case 'a':if(combat)return AttackAction{};break;
    case 'd':if(!combat)return DismissAction{};break;
    case 'u':if(combat || screen==MainScreen::None)return UseItemAction{};break;
    case InputKey::F1+8:return SaveGameAction{};
    case 9:return ControlPanelAction{};
    case 'i':return InfoAction{};case 'q':return QuickReferenceAction{};
    case 'm':return MapAction{};case 'v':if(!combat)return QuestsAction{};break;
    case '=':return ToggleAutomapAction{};
    case 0xb0:return NavigationAction::StrafeLeft;
    case 0xb1:return NavigationAction::StrafeRight;
    case InputKey::Left:return NavigationAction::TurnLeft;
    case InputKey::Right:return NavigationAction::TurnRight;
    case InputKey::Up:return NavigationAction::MoveForward;
    case InputKey::Down:return NavigationAction::MoveBackward;
    case InputKey::Escape:return CancelInteractionAction{};
    case InputKey::Space:if(!combat)return InteractionAction{};break;
    case InputKey::Enter:if(screen==MainScreen::None)return AcknowledgeAction{};break;
    case 'y':if(screen==MainScreen::None)return YesAction{};break;
    case 'n':if(screen==MainScreen::None)return NoAction{};break;
    default:break;
    }
    if(code>='1' && code<='9' && screen!=MainScreen::Exploration)
        return SelectInventorySlotAction{std::size_t(code-'1')};
    return screen==MainScreen::Exploration?std::optional<PlayerAction>{ExplorationKeyAction{code}}:std::nullopt;
}
std::optional<PlayerAction> keyAction(const SDL_KeyboardEvent &key,const InputContext &context) {
    if(russianKeyboard && context.dialog && context.dialog->textEntry) {
        // Russian text comes from key events, never host text input. The
        // shared getString (RU 0x1db72..) types 0x20-0xff and deletes on
        // Backspace and Left; numeric entries take their printable keys.
        const auto raw=dosCode(key);
        if(raw==13 || raw==27 || raw==8)return context.dialog->key(raw==13?InputKey::Enter:raw==27?InputKey::Escape:8);
        if(context.dialog->stringEntry) {
            if(raw==0xf0)return context.dialog->key(8);
            if(raw>=0x20 && raw<=0xff)return TextInputAction{std::string(1,char(raw))};
            return {};
        }
        if(raw>=0x20 && raw<=0x7e)return TextInputAction{std::string(1,char(raw))};
        const auto code=inputKey(key);
        return code?context.dialog->key(code):std::nullopt;
    }
    const auto code=inputKey(key,context);
    if(!code)return {};
    if(!context.dialog)return playerAction(code,context.mainScreen);
    // DOS text readers consume the same FIFO. A key typed before a prompt
    // opens has no SDL_TEXTINPUT packet, so retained Event keys supply text.
    if(context.retainEventKeys && context.dialog->textEntry && code>=32 && code<=126)
        return TextInputAction{std::string(1,char(code))};
    return context.dialog->key(code);
}

bool showLoop(const IndexedFrame &suppliedInitial, const std::string &title,
		const SdlWindow::FrameUpdateHandler &handler,
		const std::function<bool()> &canCancelInteraction = {},
		const SdlWindow::IdleFrameHandler &idle = {},
		const std::function<std::string()> &status = {}) {
	const auto initialBinding = suppliedInitial.presentation();
	const auto &initialFrame = initialBinding ? *initialBinding : suppliedInitial;
	bool success = false;
	struct CloseNotification {
		const SdlWindow::FrameUpdateHandler &handler;
		bool &success;
		~CloseNotification() {
			try { if (!success && handler.failed) handler.failed(); } catch (...) {}
			try { if (handler.closed) handler.closed(); } catch (...) {}
		}
	} close{handler, success};
	// The keyboard of this loop; nested loops restore their caller's.
	struct KeyboardScope {
		const XeenKeyboard *saved=russianKeyboard;
		~KeyboardScope() {russianKeyboard=saved;}
	} keyboardScope;
	russianKeyboard=handler.keyboard.get();
	try {
	if (!initialFrame.isValid()) {
		std::cerr << "Invalid indexed framebuffer.\n";
		return false;
	}

	SDL_SetMainReady();
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
	// Windows would otherwise blur-stretch the window on displays scaled above 100%.
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
	if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
		return false;
	}

	struct Resources {
		SDL_Window *window = nullptr;
		bool cursorHidden = false;
        int priorCursor=SDL_ENABLE;
		~Resources() {
			if (cursorHidden) SDL_ShowCursor(priorCursor);
			if (window) SDL_DestroyWindow(window);
			SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
		}
	} resources;
	auto &placement = placementMemory();
	if (placement.file && !placement.loaded) placement.placement = placement.saved = readWindowPlacement(*placement.file);
	placement.loaded = true;
	std::pair<int, int> windowed{initialFrame.width * 3,
        placement.placement.aspect == PictureAspect::FourThree ? initialFrame.width * 9 / 4 : initialFrame.height * 3};
	SDL_Rect usable{};
	if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
		windowed = windowedSize(placement.placement, usable.w, usable.h, initialFrame.width, initialFrame.height);
	SDL_Window *window = resources.window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED, windowed.first, windowed.second,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
		(placement.placement.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
	if (!window) {
		std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
		return false;
	}
	// Runs before Resources destroys the window.
	struct PlacementRecord {
		SDL_Window *window;
		~PlacementRecord() { try { recordWindowedSize(window); savePlacement(); } catch (...) {} }
	} placementRecord{window};
	const bool corrected = placement.placement.aspect == PictureAspect::FourThree;
	WorldGpuRenderer gpu(window, initialFrame.width, initialFrame.height, corrected);
	SDL_SetWindowMinimumSize(window, initialFrame.width, corrected ? initialFrame.width * 3 / 4 : initialFrame.height);

    if(handler.cursorImage) {
        const auto cursor=handler.cursorImage();
		gpu.cursor(cursor);
        resources.priorCursor=SDL_ShowCursor(SDL_QUERY);SDL_ShowCursor(SDL_DISABLE);resources.cursorHidden=true;
    }
    const auto mappedMouse = [&](int x, int y) -> std::optional<std::pair<int, int>> {
        int ww, wh; SDL_GetWindowSize(window, &ww, &wh);
		const auto output = gpu.outputSize();
        return windowToFrame(x, y, ww, wh, output.first, output.second, initialFrame.width, initialFrame.height,
			corrected ? PictureAspect::FourThree : PictureAspect::Square);
    };
    const auto present = [&] {
		const bool cursorEnabled = resources.cursorHidden &&
			!(handler.inputContext && handler.inputContext({}).hideCursor);
		gpu.present(cursorEnabled);
    };
    // Borderless desktop fullscreen keeps the display mode. The active
    // presentation path re-centers the picture and maps the mouse.
    const auto toggleFullscreen=[&] {
        const bool fullscreen=!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN);
        if(fullscreen)recordWindowedSize(window);
        if(SDL_SetWindowFullscreen(window,fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0)!=0) {
            std::cerr<<"Cannot change fullscreen mode: "<<SDL_GetError()<<'\n';return;
        }
        placementMemory().placement.fullscreen=fullscreen;savePlacement();
    };
	if (handler.frameCurrent && !handler.frameCurrent()) throw std::runtime_error("Stale initial frame handoff");
	IndexedFrame::Presentation uploadedFrame, presentedFrame;
	std::optional<std::uint64_t> uploadedInput;
	bool uploaded = false;
	IndexedFrame displayContent;
	bool allowModernDisplay = true;
	const auto accepts = [&](const IndexedFrame::Presentation &frame) {
		return !handler.acceptsFrame || handler.acceptsFrame(frame);
	};
	// All three upload paths use the content and identity supplied together.
	// Stale/wrong-owner frames are ignored without releasing the live boundary.
	const auto upload = [&](const IndexedFrame &supplied) {
		const auto bound = supplied.presentation();
		if (!accepts(bound)) return true;
		const auto &content = bound ? *bound : supplied;
		const auto context = handler.inputContext ? handler.inputContext(bound) : InputContext{};
		allowModernDisplay = !context.dialog && (!handler.inputContext ||
			context.mainScreen == MainScreen::Exploration || context.mainScreen == MainScreen::Combat);
		if (!uploadFrame(gpu, content, initialFrame.width, initialFrame.height, allowModernDisplay)) return false;
		displayContent=content;
		uploadedFrame = bound;
		uploaded = true;
		uploadedInput = handler.displayedInput ? handler.displayedInput() : std::nullopt;
		return true;
	};
	success = upload(suppliedInitial);
	bool running = success;
 bool cutsceneQuitPending=false;
    unsigned mouseButtons=0;
	if (running && uploaded && accepts(uploadedFrame)) {
		present();
		if (handler.frameCurrent && !handler.frameCurrent()) throw std::runtime_error("Stale initial upload");
		// Sample pending OS input while this handoff is still unacquired. Never
		// pump between successful acquisition and the queue fence below: that
		// would collect and retire a genuinely fresh press under the new frame.
		SDL_PumpEvents();
		if (handler.framePresented) handler.framePresented(uploadedFrame);
		if (handler.completeInputHandoff) handler.completeInputHandoff(uploadedFrame);
		presentedFrame = uploadedFrame;
		uploadedInput = handler.displayedInput ? handler.displayedInput() : std::nullopt;
	}
	std::uint64_t cycle = 0;
	bool spaceDown = false, blockDown = false, revisitDown = false, inspectDown = false;
	std::array<bool,SDL_NUM_SCANCODES> journeyKeys{};
	std::uint32_t readyAt = SDL_GetTicks();
    struct PendingAction { PlayerAction action; std::uint64_t context; bool repeat; std::optional<InputButton> button; int scan=0; std::uint64_t press=0; unsigned mouse=0; std::optional<SDL_KeyboardEvent> key; };
    std::deque<PendingAction> pendingActions;
    struct HeldKey {
        std::optional<PlayerAction> action;
        std::optional<InputButton> button;
        std::uint64_t context=0, press=0;
        SDL_Keycode key=0;
        bool delivered=false;
        std::uint64_t repeatAt=0;
    };
    std::array<HeldKey,SDL_NUM_SCANCODES> heldKeys{};
    std::uint64_t pressIdentity=0,lastRepeat=0,heldContext=0;
    Uint16 heldModifiers=0;
    struct StrictMouse {PlayerAction action;std::optional<std::uint64_t> input;IndexedFrame::Presentation frame;std::uint64_t context;};
    std::optional<StrictMouse> strictMouse;
    std::optional<InputButton> mouseFeedback;
    bool actionUsed = false;
    const auto contextFor = [&](const IndexedFrame::Presentation &origin) {
        return handler.inputContext ? handler.inputContext(origin) : InputContext{};
    };
    const auto navigationNow = [&] {
        return handler.presentationClock ? handler.presentationClock() : SdlWindow::presentationNanoseconds();
    };
    auto acquiredContext = contextFor(presentedFrame);
    auto keyboardClear=acquiredContext.keyboardClear,mouseClear=acquiredContext.mouseClear;
    const auto clearKeyboard = [&](bool markNativeMakes=true) {
        pendingActions.erase(std::remove_if(pendingActions.begin(),pendingActions.end(),[](const auto &p){return !p.mouse;}),pendingActions.end());
        for(auto &held:heldKeys)if(held.press)held.delivered=true;
        if(!markNativeMakes)return; // NumLock release precedes later native makes.
        // Keep native make/break order for physical state and future repeats.
        // Only the code is cleared; modifiers and host actions remain ordinary.
        SDL_FilterEvents([](void *,SDL_Event *queued) -> int {
            if(queued->type==SDL_KEYDOWN && inputKey(queued->key) &&
                !fullscreenToggleKey(queued->key.keysym.sym,queued->key.keysym.mod))queued->key.repeat=clearedKeyMake;
            return 1;
        },nullptr);
    };
    const auto synchronizeQueue = [&](const InputContext &context) {
        if(mouseClear!=context.mouseClear) {
            mouseClear=context.mouseClear;strictMouse.reset();
            pendingActions.erase(std::remove_if(pendingActions.begin(),pendingActions.end(),[](const auto &p){return p.mouse!=0;}),pendingActions.end());
            SDL_FilterEvents([](void *,SDL_Event *queued) -> int {return queued->type!=SDL_MOUSEBUTTONDOWN;},nullptr);
        }
        if(keyboardClear!=context.keyboardClear) {
            keyboardClear=context.keyboardClear;
            clearKeyboard();
        }
        if (!context.acceptsQueuedInput || heldContext!=context.contextId ||
            (!pendingActions.empty() && pendingActions.front().context != context.contextId)) {
            if(context.retainEventKeys) {
                pendingActions.erase(std::remove_if(pendingActions.begin(),pendingActions.end(),[](const auto &p){return !p.key;}),pendingActions.end());
                for(auto &p:pendingActions)p.context=context.contextId;
                // Navigation keeps M63's modal/context fences. Ordinary held keys
                // survive, with their next make translated by the current reader.
                for(auto &held:heldKeys) {
                    if(!held.action || std::holds_alternative<NavigationAction>(*held.action))held={};
                    else held.context=context.contextId;
                }
            } else {pendingActions.clear();heldKeys.fill({});lastRepeat=0;}
        }
        heldContext=context.contextId;
    };
    const auto retireQueuedKeys = [&] {
        // The strict path retains the pre-acquisition timestamp fence.
        if (contextFor(presentedFrame).acceptsQueuedInput || contextFor(presentedFrame).retainEventKeys) return;
        SDL_FilterEvents([](void *context, SDL_Event *queued) -> int {
            if (queued->type == SDL_KEYDOWN)
                queued->key.timestamp = *static_cast<std::uint32_t *>(context) - 1;
            if (queued->type == SDL_MOUSEBUTTONDOWN)
                queued->button.timestamp = *static_cast<std::uint32_t *>(context) - 1;
            if (queued->type == SDL_TEXTINPUT)
                queued->text.timestamp = *static_cast<std::uint32_t *>(context) - 1;
            return 1;
        }, &readyAt);
    };
    retireQueuedKeys();
    std::optional<std::uint64_t> displayedInput = uploaded && accepts(uploadedFrame) && handler.displayedInput ? handler.displayedInput() : std::nullopt;
    const auto inputCurrent = [&](const IndexedFrame::Presentation &origin) {
        return handler.acceptsInputFrame ? handler.acceptsInputFrame(origin) :
            uploaded && uploadedFrame == origin && accepts(origin);
    };
    const auto restoreMouseFeedback = [&] {
        if(!mouseFeedback)return;
        if(!uploadFrame(gpu,displayContent,initialFrame.width,initialFrame.height,allowModernDisplay))throw std::runtime_error("Mouse feedback restoration failed");
        present();mouseFeedback.reset();
    };
    const auto feedback = [&](const InputButton &button,const IndexedFrame::Presentation &origin,bool restore=true) {
        if(handler.drawButton && inputCurrent(origin)) {
            // DOS shows frame | 1 and restores immediately for keyboard input.
            // Mouse feedback retains the native texture until physical release.
            // No gameplay callback, idle update, semantic upload or frame
            // acquisition occurs during this native display effect.
            auto pressed=displayContent;
            handler.drawButton(pressed,button);
            if(!inputCurrent(origin) || (handler.frameCurrent && !handler.frameCurrent()))
                throw std::runtime_error("Stale button feedback origin");
            const auto presentButtonTexture=[&] {
                present();
            };
            const auto presentButton=[&](const IndexedFrame &frame) {
                if(!uploadFrame(gpu,frame,initialFrame.width,initialFrame.height,allowModernDisplay))
                    throw std::runtime_error("Button feedback upload failed");
                presentButtonTexture();
            };
            presentButton(pressed);
            // Keyboard feedback has no explicit dwell (DAT 0x0e1ca..0x0e2b1).
            // Mouse release is handled below before entering gameplay.
            if(restore)presentButton(displayContent);else mouseFeedback=button;
            if(!inputCurrent(origin) || (handler.frameCurrent && !handler.frameCurrent()))
                throw std::runtime_error("Stale button feedback restoration");
        }
    };
    const auto deliver = [&](const PlayerAction &action, const std::optional<std::uint64_t> &input,
            const IndexedFrame::Presentation &origin,const std::optional<InputButton> &button=std::nullopt) {
        try {
            actionUsed = true;
            if(button) feedback(*button,origin);
            const auto nextFrame = input && handler.withPresentedInput ?
                handler.withPresentedInput(action,*input,origin) :
                input && handler.withDisplayedInput ? handler.withDisplayedInput(action,*input) : handler(action);
            synchronizeQueue(contextFor(presentedFrame));
            if (handler.frameCurrent && !handler.frameCurrent()) throw std::runtime_error("Stale gameplay frame handoff");
            if (nextFrame && !upload(*nextFrame)) { success = false; running = false; }
        } catch (const std::exception &error) {
            std::cerr << "Scene update failed: " << error.what() << '\n';
            success = false; running = false;
        }
    };
    const auto dispatchEvent = [&](const SDL_Event &event, const std::optional<std::uint64_t> &batchInput,
            const IndexedFrame::Presentation &batchFrame, const InputContext &batchContext) {
        if (event.type == SDL_MOUSEMOTION) {
            if (event.motion.windowID == SDL_GetWindowID(window)) gpu.mouseMoved();
            return;
        }
        if(event.type==SDL_RENDER_TARGETS_RESET || event.type==SDL_RENDER_DEVICE_RESET) {
			// This loop no longer owns an SDL renderer or its reset notifications.
            return;
        }
        if(handler.ignoreMouse && (event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP))return;
        if(event.type==SDL_AUDIODEVICEREMOVED && !event.adevice.iscapture) {
            SdlAudioDevice::removed(event.adevice.which);return;
        }
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F11) {
			if (!event.key.repeat) {
				modernWorld = !modernWorld;
				try {
					if (!uploadFrame(gpu, displayContent, initialFrame.width,
							initialFrame.height, allowModernDisplay))
						throw std::runtime_error("Cannot upload switched world renderer");
					present();
					std::cerr << "World renderer: " << (modernWorld ? "experimental GPU" : "original") << '\n';
				} catch (const std::exception &error) {
					modernWorld = false;
					std::cerr << "World GPU renderer failed: " << error.what() << '\n';
					SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "World GPU renderer", error.what(), window);
					if (!uploadFrame(gpu, displayContent, initialFrame.width,
							initialFrame.height, allowModernDisplay))
						throw std::runtime_error("Cannot restore original renderer");
					present();
				}
			}
			return;
		}
		if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_F11) return;
		if (event.type == SDL_WINDOWEVENT && event.window.windowID != SDL_GetWindowID(window)) return;
		if (event.type == SDL_WINDOWEVENT) gpu.refreshWindow();
        // Host window control, as in DOSBox: the game never receives Alt+Enter,
        // including its repeats; a plain Enter is unaffected.
        if(event.type==SDL_KEYDOWN && fullscreenToggleKey(event.key.keysym.sym,event.key.keysym.mod)) {
            if(!event.key.repeat)toggleFullscreen();
            return;
        }
        // XEEN.EXE 0x11ab9..0x11af8; not the DAT's title/gameplay input.
        if(handler.cutsceneControlQuit && event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_q && (event.key.keysym.mod & KMOD_CTRL)) {
            pendingActions.clear();cutsceneQuitPending=true;
            if(!handler.cutsceneControlQuitReady || handler.cutsceneControlQuitReady())running=false;
            return;
        }
        if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED)recordWindowedSize(window);
        if(handler.finished && handler.finished())return;
        if (event.type == SDL_QUIT) {
            pendingActions.clear(); running = false;
        } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            journeyKeys.fill(false);heldKeys.fill({});
            heldModifiers=0;
            spaceDown = blockDown = revisitDown = inspectDown = false;
            mouseButtons=0;strictMouse.reset();restoreMouseFeedback();
            pendingActions.clear();
        } else if(event.type==SDL_MOUSEBUTTONUP) {
            mouseButtons&=~SDL_BUTTON(event.button.button);
            if(!mouseButtons)restoreMouseFeedback();
        } else if (event.type == SDL_KEYUP) {
            if(event.key.keysym.sym==SDLK_NUMLOCKCLEAR) {
                clearKeyboard(false);
            }
            const auto scan = physicalScan(event.key);
            if (scan > SDL_SCANCODE_UNKNOWN && scan < SDL_NUM_SCANCODES) {
                journeyKeys[scan] = false;heldKeys[scan]={};
            }
            if(event.key.keysym.sym==SDLK_LCTRL || event.key.keysym.sym==SDLK_RCTRL ||
                event.key.keysym.sym==SDLK_LSHIFT || event.key.keysym.sym==SDLK_RSHIFT ||
                event.key.keysym.sym==SDLK_LALT || event.key.keysym.sym==SDLK_RALT) {
                heldModifiers=event.key.keysym.mod;
                const auto released=event.key.keysym.sym;
                heldModifiers&=~(released==SDLK_LCTRL?KMOD_LCTRL:released==SDLK_RCTRL?KMOD_RCTRL:
                    released==SDLK_LSHIFT?KMOD_LSHIFT:released==SDLK_RSHIFT?KMOD_RSHIFT:
                    released==SDLK_LALT?KMOD_LALT:KMOD_RALT);
                const auto context=contextFor(batchFrame);
                for(auto &held:heldKeys)if(held.press) {
                    SDL_KeyboardEvent key{};key.keysym.sym=held.key;key.keysym.mod=heldModifiers;
                    auto resolved=keyAction(key,context);
                    if(!resolved && context.retainEventKeys && inputKey(key))resolved=ExplorationKeyAction{inputKey(key)};
                    held.action=resolved;
                    held.button=xeenMainScreenKeyButton(held.action,inputKey(key,context),context.mainScreen);
                }
            }
            if (event.key.keysym.sym == SDLK_SPACE) spaceDown = false;
            if (event.key.keysym.sym == SDLK_b) blockDown = false;
            if (event.key.keysym.sym == SDLK_r) revisitDown = false;
            if (event.key.keysym.sym == SDLK_i) inspectDown = false;
        } else if(event.type==SDL_TEXTINPUT) {
            const auto context=contextFor(batchFrame);
            // Russian text comes from key events (keyAction).
            if(!russianKeyboard && context.dialog && context.dialog->textEntry && !context.retainEventKeys && context.readyForAction && inputCurrent(batchFrame) &&
                context.contextId==batchContext.contextId && static_cast<std::int32_t>(event.text.timestamp-readyAt)>=0)
                deliver(TextInputAction{event.text.text},batchInput,batchFrame,{});
        } else if (event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN) {
            const bool mouse = event.type == SDL_MOUSEBUTTONDOWN;
            const auto context = contextFor(batchFrame);
            synchronizeQueue(context);
            if(mouse)mouseButtons|=SDL_BUTTON(event.button.button);
            // The CD mouse callback stores coordinates only for either button.
            if(mouse && strictMouse)return;
            if(!mouse) switch(event.key.keysym.sym) {
            case SDLK_LCTRL: case SDLK_RCTRL: case SDLK_LALT: case SDLK_RALT:
            case SDLK_LSHIFT: case SDLK_RSHIFT: case SDLK_LGUI: case SDLK_RGUI:
                heldModifiers=event.key.keysym.mod;
                if(event.key.keysym.sym==SDLK_LCTRL || event.key.keysym.sym==SDLK_RCTRL)heldModifiers|=event.key.keysym.sym==SDLK_LCTRL?KMOD_LCTRL:KMOD_RCTRL;
                for(auto &held:heldKeys)if(held.press) {
                    SDL_KeyboardEvent key{};key.keysym.sym=held.key;key.keysym.mod=heldModifiers;
                    auto resolved=keyAction(key,context);
                    if(!resolved && context.retainEventKeys && inputKey(key))resolved=ExplorationKeyAction{inputKey(key)};
                    held.action=resolved;
                    held.button=xeenMainScreenKeyButton(held.action,inputKey(key,context),context.mainScreen);
                }
                return;
            default:break;
            }
            const auto point = mouse ? mappedMouse(event.button.x,event.button.y) : std::optional<std::pair<int,int>>{};
            const int mouseX = point ? point->first : -1, mouseY = point ? point->second : -1;
            if (mouse && ((event.button.button != SDL_BUTTON_LEFT && event.button.button!=SDL_BUTTON_RIGHT) || (!context.dialog && context.mainScreen == MainScreen::None) ||
                initialFrame.width != 320 || initialFrame.height != 200 ||
                mouseX<0 || mouseY<0 || mouseX>=320 || mouseY>=200)) return;
            // SDL maps events in square mode; corrected mode maps native window
            // coordinates through the same drawable rectangle used to present.
            auto action = mouse ? (context.dialog?context.dialog->click(mouseX,mouseY):
                xeenMainScreenClick(mouseX,mouseY,context.mainScreen,russianKeyboard)):keyAction(event.key,context);
            if(!context.dialog && action)if(const auto *member=std::get_if<SelectMemberAction>(&*action))
                if(member->partyIndex>=context.partySize)action.reset();
            if(!mouse && !action && context.retainEventKeys && inputKey(event.key))action=ExplorationKeyAction{inputKey(event.key)};
            auto button=context.dialog ?
                (action && std::get_if<DialogKeyAction>(&*action) ? context.dialog->button(std::get<DialogKeyAction>(*action).key) : std::nullopt) :
                mouse ? xeenMainScreenButtonAt(mouseX,mouseY,context.mainScreen) :
                    xeenMainScreenKeyButton(action,inputKey(event.key,context),context.mainScreen);
            // A Russian button whose value no dispatch case matches shows its
            // press, then does nothing.
            if(mouse && russianKeyboard && context.dialog && context.dialog->keySet && action)
                if(auto *clicked=std::get_if<DialogKeyAction>(&*action))
                    clicked->key=russianKeyboard->click(context.dialog->keySet,clicked->key);
            if(!mouse && !context.dialog && !russianKeyboard && (event.key.keysym.mod & KMOD_CTRL)) {
                button=event.key.keysym.sym==SDLK_LEFT ? xeenMainScreenButtonAt(235,169,context.mainScreen) :
                    event.key.keysym.sym==SDLK_RIGHT ? xeenMainScreenButtonAt(286,169,context.mainScreen) : std::nullopt;
            }
            if (!mouse && !context.retainEventKeys && event.key.keysym.sym == SDLK_ESCAPE && !event.key.repeat) pendingActions.clear();
            const bool movement = action && std::holds_alternative<NavigationAction>(*action);
            const auto *slot = action ? std::get_if<SelectInventorySlotAction>(&*action) : nullptr;
            const bool queueKey = action && (movement || std::holds_alternative<AttackAction>(*action) || std::holds_alternative<InteractionAction>(*action) ||
                std::holds_alternative<BlockAction>(*action) || std::holds_alternative<BashAction>(*action) || std::holds_alternative<ShootAction>(*action) ||
                std::holds_alternative<RestAction>(*action) || std::holds_alternative<DismissAction>(*action) || std::holds_alternative<ControlPanelAction>(*action) ||
                std::holds_alternative<QuickReferenceAction>(*action) || std::holds_alternative<InfoAction>(*action) ||
                std::holds_alternative<MapAction>(*action) || std::holds_alternative<QuestsAction>(*action) || std::holds_alternative<ToggleAutomapAction>(*action) ||
                std::holds_alternative<RevisitCompletedAction>(*action) || std::holds_alternative<WaitAction>(*action) ||
                std::holds_alternative<QuickFightAction>(*action) || std::holds_alternative<QuickFightOptionsAction>(*action) ||
                std::holds_alternative<CastSpellAction>(*action) || std::holds_alternative<SelectMemberAction>(*action) ||
                (context.mainScreen==MainScreen::Combat && std::holds_alternative<UseItemAction>(*action)) ||
                std::holds_alternative<UnsupportedMainScreenAction>(*action) || std::holds_alternative<ExplorationKeyAction>(*action) ||
                (context.dialog && (std::holds_alternative<DialogKeyAction>(*action) ||
                    std::holds_alternative<AcknowledgeAction>(*action) || std::holds_alternative<CancelInteractionAction>(*action))) ||
                (slot && slot->slot < 3));
            const bool queueable = (context.acceptsQueuedInput && queueKey) || (!mouse && context.retainEventKeys && inputKey(event.key));
            const auto scan = mouse ? SDL_SCANCODE_UNKNOWN : physicalScan(event.key);
            if(!mouse && event.key.repeat==clearedKeyMake) {
                if(scan>SDL_SCANCODE_UNKNOWN && scan<SDL_NUM_SCANCODES && !journeyKeys[scan]) {
                    journeyKeys[scan]=true;heldModifiers=event.key.keysym.mod;
                    if(action) {
                        if(pressIdentity==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Key press identity exhausted");
                        heldKeys[scan]={action,button,context.contextId,++pressIdentity,event.key.keysym.sym,true,
                            PresentationClock::checkedAdd(navigationNow(),navigationRepeatDelay)};
                    }
                }
                return;
            }
            if (queueable) {
                // A context transition within this event batch cannot relabel old keys.
                if (context.contextId != batchContext.contextId && !context.retainEventKeys) return;
                if (mouse) {
                    if (pendingActions.size() < 5) {
                        if(button)feedback(*button,batchFrame,false);
                        pendingActions.push_back({*action,context.contextId,false,{},0,0,
                            static_cast<unsigned int>(SDL_BUTTON(event.button.button))});
                    }
                    return;
                }
                if (scan <= SDL_SCANCODE_UNKNOWN || scan >= SDL_NUM_SCANCODES) return;
                const bool held = journeyKeys[scan];
                if (event.key.repeat) {
                    // OS repeats are observations, never accumulated movement.
                    if(held && heldKeys[scan].press) {
                        heldKeys[scan].action=action;
                        heldKeys[scan].button=button;
                        heldKeys[scan].key=event.key.keysym.sym;
                    }
                    return;
                }
                if(held)return;
                journeyKeys[scan]=true;heldModifiers=event.key.keysym.mod;
                if(action) {
                    if(pressIdentity==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Navigation press identity exhausted");
                    const auto press=++pressIdentity;
                    heldKeys[scan]={action,button,context.contextId,press,event.key.keysym.sym,false,
                        PresentationClock::checkedAdd(navigationNow(),navigationRepeatDelay)};
                    // M63 navigation taps remain unbounded. DOS Event keys use
                    // ten FIFO slots; held demand is stored separately.
                    if(movement || pendingActions.size()<(context.retainEventKeys || context.mainScreen==MainScreen::Exploration?10u:5u))
                        pendingActions.push_back({*action,context.contextId,false,button,int(scan),press,0,
                            !movement && context.retainEventKeys?std::optional<SDL_KeyboardEvent>{event.key}:std::nullopt});
                } else if(pendingActions.size()<5)pendingActions.push_back({*action,context.contextId,false,button});
                return;
            }
            if(context.dialog && (!context.readyForAction || !inputCurrent(batchFrame) || context.contextId!=batchContext.contextId)) return;
            if(mouse) {
                if(context.dialog && context.readyForAction && inputCurrent(batchFrame) &&
                    context.contextId==batchContext.contextId && static_cast<std::int32_t>(event.button.timestamp-readyAt)>=0 && action)
                {
                    if(button)feedback(*button,batchFrame,false);
                    strictMouse=StrictMouse{*action,batchInput,batchFrame,context.contextId};
                }
                return;
            }
            const bool textBackspace=context.dialog && context.dialog->textEntry && event.key.keysym.sym==SDLK_BACKSPACE;
            if (event.key.repeat != 0 && !textBackspace) return;
            if ((handler.protectAllKeys || context.dialog) && (action || (button && handler.drawButton))) {
                if (scan <= SDL_SCANCODE_UNKNOWN || scan >= SDL_NUM_SCANCODES) return;
                const bool held = journeyKeys[scan]; journeyKeys[scan] = true;
                if ((held && !textBackspace) || (!held && event.key.repeat) || static_cast<std::int32_t>(event.key.timestamp-readyAt) < 0) return;
            } else if (batchInput && (event.key.keysym.sym == SDLK_SPACE || event.key.keysym.sym == SDLK_b ||
                event.key.keysym.sym == SDLK_r || event.key.keysym.sym == SDLK_i)) {
                auto &down = event.key.keysym.sym == SDLK_SPACE ? spaceDown : event.key.keysym.sym == SDLK_b ? blockDown :
                    event.key.keysym.sym == SDLK_r ? revisitDown : inspectDown;
                const bool held = down; down = true;
                if (held || static_cast<std::int32_t>(event.key.timestamp-readyAt) < 0) return;
            }
            if (handler.withPresentedInput && static_cast<std::int32_t>(event.key.timestamp-readyAt) < 0) return;
            if(mouseButtons)return; // Common mouse input returns only after release.
            if (handler.protectAllKeys && event.key.keysym.sym == SDLK_ESCAPE &&
                (batchInput != (handler.displayedInput ? handler.displayedInput() : std::nullopt) || !inputCurrent(batchFrame))) return;
            if (!context.dialog && event.key.keysym.sym == SDLK_ESCAPE && !(handler && canCancelInteraction && canCancelInteraction())) {
                // Nothing to cancel: the original main screen ignores Escape (no
                // Escape entry in the pinned Interface::perform). The game ends
                // through the control panel's Quit, Alt+F4 or the close button.
            } else if (handler && action && ((!handler.acceptsFrame && !handler.withPresentedInput) || inputCurrent(batchFrame))) {
                deliver(*action,batchInput,batchFrame,button);
            } else if(!action && button && handler.drawButton && context.readyForAction && inputCurrent(batchFrame)) {
                // Cosmetic response only: no PlayerAction is created or queued.
                try {feedback(*button,batchFrame);}
                catch(const std::exception &error) {
                    std::cerr<<"Button feedback failed: "<<error.what()<<'\n';success=false;running=false;
                }
            }
        }
    };
    const auto releaseMouseAction=[&] {
        if(!strictMouse || mouseButtons)return;
        const auto next=*strictMouse;strictMouse.reset();
        const auto context=contextFor(presentedFrame);
        if(context.contextId==next.context && context.readyForAction && inputCurrent(next.frame))
            deliver(next.action,next.input,next.frame,{});
    };
	while (running) {
		try {
			if (cycle == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("SDL loop cycle overflow");
			if (handler.beginCycle) handler.beginCycle(++cycle);
		} catch (...) { success = false; break; }
		SDL_Event event;
		const auto batchInput = displayedInput;
		const auto batchFrame = presentedFrame;
		const auto batchContext = contextFor(batchFrame);
		// Start or stop text input only when it changes: in Steam Deck Game Mode each call
		// sends Steam an open- or close-keyboard request.
		const bool textInput=batchContext.dialog && batchContext.dialog->textEntry && !russianKeyboard;
		if(textInput!=bool(SDL_IsTextInputActive())) {if(textInput)SDL_StartTextInput();else SDL_StopTextInput();}
		synchronizeQueue(batchContext);
        const auto actionReady=[&](const PlayerAction &a,const InputContext &c) {
            return c.readyForAction || (std::holds_alternative<NavigationAction>(a) && c.readyForNavigation) ||
                (c.restAvailable && std::holds_alternative<RestAction>(a));
        };
        bool queuedReady=!pendingActions.empty() && actionReady(pendingActions.front().action,batchContext) &&
            !(pendingActions.front().mouse & mouseButtons);
        std::optional<std::uint64_t> nextRepeat;
        if(pendingActions.empty())for(const auto &held:heldKeys)
            if(held.action && held.delivered && held.context==batchContext.contextId && actionReady(*held.action,batchContext))
                nextRepeat=nextRepeat?std::min(*nextRepeat,held.repeatAt):held.repeatAt;
        const auto repeatSample=nextRepeat?navigationNow():0;
        const bool repeatReady=nextRepeat && repeatSample>=*nextRepeat;
        const auto waitSample=SdlWindow::presentationNanoseconds();
        int wait=int(PresentationClock::ceilMilliseconds(PresentationClock::after(waitSample,1)-waitSample));
        if(mouseButtons)wait=10;
        else if(queuedReady || repeatReady)wait=0;
        else if(handler.nextPresentationDeadline) {
            const auto deadline=handler.nextPresentationDeadline();
            if(deadline) {
                const auto now=handler.presentationClock?handler.presentationClock():SdlWindow::presentationNanoseconds();
                wait=*deadline<=now?0:int(std::min<std::uint64_t>(1000,PresentationClock::ceilMilliseconds(*deadline-now)));
            }
        }
        if(nextRepeat && *nextRepeat>repeatSample && !mouseButtons)
            wait=std::min(wait,int(std::min<std::uint64_t>(1000,PresentationClock::ceilMilliseconds(*nextRepeat-repeatSample))));
        // The render owner redraws independently, including while callbacks run.
        // Motion alone must not start another gameplay cycle.
        const auto semanticWake = PresentationClock::checkedAdd(SdlWindow::presentationNanoseconds(),
            std::uint64_t(wait) * 1000000);
        bool receivedEvent = false;
        for (;;) {
            const auto now = SdlWindow::presentationNanoseconds();
            const int remaining = now >= semanticWake ? 0 :
                int(PresentationClock::ceilMilliseconds(semanticWake - now));
            receivedEvent = SDL_WaitEventTimeout(&event, remaining);
            if (receivedEvent && event.type == SDL_MOUSEMOTION &&
                    event.motion.windowID == SDL_GetWindowID(window)) {
                gpu.mouseMoved();
                receivedEvent = false;
                if (SdlWindow::presentationNanoseconds() >= semanticWake) break;
                continue;
            }
            break;
        }
        if (receivedEvent) {
			do {
				dispatchEvent(event,batchInput,batchFrame,batchContext);
			} while (running && SDL_PollEvent(&event));
		}
		if (!running) break;
        if(cutsceneQuitPending && (!handler.cutsceneControlQuitReady || handler.cutsceneControlQuitReady()))break;
        if(handler.finished && handler.finished())break;
        const auto drainContext = contextFor(presentedFrame);
        synchronizeQueue(drainContext);
        releaseMouseAction();
        const bool hasHeldRepeat=std::any_of(heldKeys.begin(),heldKeys.end(),[&](const auto &held) {
            return held.action && held.delivered && held.context==drainContext.contextId;
        });
        const auto drainRepeatNow=hasHeldRepeat?navigationNow():0;
        if (!mouseButtons && !actionUsed && handler && !pendingActions.empty() && (drainContext.acceptsQueuedInput || drainContext.retainEventKeys) &&
            actionReady(pendingActions.front().action,drainContext) && inputCurrent(presentedFrame) &&
            !(pendingActions.front().mouse & mouseButtons)) {
            const auto next=pendingActions.front();
            pendingActions.pop_front(); // A ready refusal is consumed, never retried.
            if(next.scan && heldKeys[next.scan].press==next.press) {
                auto &held=heldKeys[next.scan];held.delivered=true;
                held.repeatAt=std::max(held.repeatAt,PresentationClock::checkedAdd(navigationNow(),navigationRepeatPeriod));
            }
            if(next.key) {
                const auto translated=keyAction(*next.key,drainContext);
                if(translated)deliver(*translated,displayedInput,presentedFrame,
                    drainContext.dialog?drainContext.dialog->button(inputKey(*next.key,drainContext)):xeenMainScreenKeyButton(translated,inputKey(*next.key,drainContext),drainContext.mainScreen));
            } else deliver(next.action,displayedInput,presentedFrame,next.button);
        } else if(hasHeldRepeat && !mouseButtons && !actionUsed && handler && pendingActions.empty() && (drainContext.acceptsQueuedInput || drainContext.retainEventKeys) && inputCurrent(presentedFrame)) {
            HeldKey *selected=nullptr,*first=nullptr;
            const auto now=drainRepeatNow;
            for(auto &held:heldKeys)if(held.action && held.delivered && held.context==drainContext.contextId &&
                now>=held.repeatAt && actionReady(*held.action,drainContext)) {
                if(!first || held.press<first->press)first=&held;
                if(held.press>lastRepeat && (!selected || held.press<selected->press))selected=&held;
            }
            if(!selected)selected=first;
            if(selected){
                lastRepeat=selected->press;
                // Reset at dispatch, not after fallible feedback/work; late wakes
                // supply one demand and never accumulate typematic catch-up.
                selected->repeatAt=PresentationClock::checkedAdd(now,navigationRepeatPeriod);
                if(drainContext.retainEventKeys && !std::holds_alternative<NavigationAction>(*selected->action)) {
                    SDL_KeyboardEvent key{};key.keysym.sym=selected->key;key.keysym.mod=heldModifiers;
                    const auto translated=keyAction(key,drainContext);
                    if(translated)deliver(*translated,displayedInput,presentedFrame,
                        drainContext.dialog?drainContext.dialog->button(inputKey(key,drainContext)):xeenMainScreenKeyButton(translated,inputKey(key,drainContext),drainContext.mainScreen));
                } else deliver(*selected->action,displayedInput,presentedFrame,selected->button);
            }
        }
        if (!running) break;
        const auto waitingForDraw=[&](const PlayerAction &action) {
            return std::holds_alternative<NavigationAction>(action)?drainContext.navigationDrawPending:drainContext.actionDrawPending;
        };
        const bool inputWaitingForDraw=
            ((!pendingActions.empty() && waitingForDraw(pendingActions.front().action)) ||
             (pendingActions.empty() && std::any_of(heldKeys.begin(),heldKeys.end(),[&](const auto &held) {
                 return held.action && waitingForDraw(*held.action) && held.delivered && held.context==drainContext.contextId && drainRepeatNow>=held.repeatAt;
             })));
        // draw3d completes before polling input (DAT 0x0d1c3..0x0d1c9). If the
        // clock crosses its threshold after admission was sampled, return to
        // that poll before allowing idle to start a second draw. Retained work
        // never sets this flag and continues through its existing idle owner.
		if (idle && !mouseButtons && !inputWaitingForDraw) {
			try {
				const auto nextFrame = idle();
				if (handler.frameCurrent && !handler.frameCurrent()) throw std::runtime_error("Stale idle frame handoff");
				if (nextFrame && !upload(*nextFrame)) {
					success = false; break;
				}
			} catch (const std::exception &error) {
				std::cerr << "Presentation update failed: " << error.what() << '\n';
				success = false;
				break;
			}
		}

		try {
			if (status) {
				const auto title = status();
				if (handler.frameCurrent && !handler.frameCurrent()) throw std::runtime_error("Stale status callback");
				diagnosticNotice(title);
				SDL_SetWindowTitle(window, title.c_str());
			}
		} catch (...) { success = false; break; }
		if (!uploaded || !accepts(uploadedFrame)) continue;
		present();
        if(mouseButtons)continue; // Release wait transports cursor pixels only.
		if (handler.frameCurrent && !handler.frameCurrent()) throw std::runtime_error("Stale presented frame");
		if (uploadedInput != (handler.displayedInput ? handler.displayedInput() : std::nullopt))
			throw std::runtime_error("Current frame was not uploaded");
		SDL_PumpEvents();
		const auto acquired = uploadedFrame;
		if (handler.framePresented) handler.framePresented(acquired);
		const bool cosmetic = acquired != presentedFrame && handler.acceptsInputFrame &&
			handler.completeInputHandoff && handler.acceptsInputFrame(presentedFrame);
		if (cosmetic) {
			// The queue cut is the input handoff's linearization point. B is fully
			// acquired; the bounded existing batch still belongs to live A. Never
			// pump or relabel it as B. Keys arriving after this cut belong to B.
			struct Batch { std::vector<SDL_Event> events; bool failed = false; } pending;
			SDL_FilterEvents([](void *context, SDL_Event *queued) -> int {
				auto &batch = *static_cast<Batch *>(context);
				try { batch.events.push_back(*queued); } catch (...) { batch.failed = true; }
				return 0;
			}, &pending);
			if (pending.failed) throw std::runtime_error("Cannot retain acquired input batch");
			// Preserve non-key event order too: a queued close precedes later keys.
			for (const auto &event : pending.events) {
				if (!running) break;
				dispatchEvent(event,displayedInput,presentedFrame,acquiredContext);
			}
            if(!running)break; // A queued close cancels the retained response.
            // A mouse action from this bounded A batch returns on release,
            // before A is retired. Holding it keeps the acquired B handoff
            // pending; never relabel the press as B or run semantic idle work.
            releaseMouseAction();
			if (!running) break;
            if(mouseButtons)continue;
			// An A response can supersede B. Its semantic successor must pass
			// the ordinary strict pre-acquisition fence before accepting input.
			if (uploadedFrame != acquired || !accepts(acquired)) continue;
		}
		if (handler.completeInputHandoff) handler.completeInputHandoff(acquired);
		const bool concreteChanged = presentedFrame != uploadedFrame;
		presentedFrame = uploadedFrame;
		uploadedInput = handler.displayedInput ? handler.displayedInput() : std::nullopt;
		const auto nextInput = handler.displayedInput ? handler.displayedInput() : std::nullopt;
		if (!cosmetic && (concreteChanged || nextInput != displayedInput)) { readyAt = SDL_GetTicks(); retireQueuedKeys(); }
        const auto nextContext = contextFor(presentedFrame);
        synchronizeQueue(nextContext);
        if (handler.inputContext && acquiredContext.contextId != nextContext.contextId && !nextContext.retainEventKeys) {
            // clearEvents boundary: retain releases, non-input events and the
            // host fullscreen toggle in order.
            SDL_FilterEvents([](void *, SDL_Event *queued) -> int {
                if(queued->type==SDL_KEYDOWN)return fullscreenToggleKey(queued->key.keysym.sym,queued->key.keysym.mod);
                return queued->type != SDL_MOUSEBUTTONDOWN;
            },nullptr);
        }
        acquiredContext = nextContext;
        actionUsed = false;
		displayedInput = nextInput;
	}

	return success;
	} catch (const std::exception &error) {
		success = false;
		std::cerr << "SDL presentation failed: " << error.what() << '\n';
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MMModern graphics error", error.what(), nullptr);
		return false;
	} catch (...) {
		success = false;
		const char *message = "Unknown native graphics error. See the MMModern log for details.";
		std::cerr << "SDL presentation failed: " << message << '\n';
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MMModern graphics error", message, nullptr);
		return false;
	}
}

} // namespace

std::uint64_t SdlWindow::presentationNanoseconds() {
    const auto frequency=SDL_GetPerformanceFrequency(),counter=SDL_GetPerformanceCounter();
    if(!frequency)throw std::runtime_error("SDL monotonic clock unavailable");
    const auto seconds=counter/frequency,remainder=counter%frequency;
    return seconds*1000000000+(remainder*1000000000)/frequency;
}

void SdlWindow::rememberPlacement(std::optional<std::filesystem::path> file) {
	auto &memory = placementMemory();
	memory.file = std::move(file);
	memory.loaded = false;
}

void SdlWindow::useAspectSetting(const std::optional<std::filesystem::path> &file) {
    placementMemory().placement.aspect = file ? readWindowPlacement(*file).aspect : PictureAspect::FourThree;
}

bool SdlWindow::show(const IndexedFrame &frame, const std::string &title) const {
	return showLoop(frame, title, FrameUpdateHandler{});
}

bool SdlWindow::showInteractive(const IndexedFrame &frame, const std::string &title,
		const FrameUpdateHandler &handler, const std::function<bool()> &handlesEscape,
		const IdleFrameHandler &idle, const std::function<std::string()> &status) const {
	return showLoop(frame, title, handler, handlesEscape, idle, status);
}

} // namespace mmodern
