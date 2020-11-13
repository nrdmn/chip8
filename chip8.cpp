#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <stack>

#include <SDL2/SDL.h>

static const std::array<Uint8, 16> keys {
    SDL_SCANCODE_X,
    SDL_SCANCODE_1,
    SDL_SCANCODE_2,
    SDL_SCANCODE_3,
    SDL_SCANCODE_Q,
    SDL_SCANCODE_W,
    SDL_SCANCODE_E,
    SDL_SCANCODE_A,
    SDL_SCANCODE_S,
    SDL_SCANCODE_D,
    SDL_SCANCODE_Z,
    SDL_SCANCODE_C,
    SDL_SCANCODE_4,
    SDL_SCANCODE_R,
    SDL_SCANCODE_F,
    SDL_SCANCODE_V,
};

static SDL_AudioSpec audio_spec;
static SDL_AudioDeviceID audio_dev = 0;
static SDL_Window *window = nullptr;
static SDL_Renderer *renderer = nullptr;
static SDL_Texture *texture = nullptr;
static SDL_TimerID timer = 0;

static uint8_t rnd() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return std::uniform_int_distribution<>(0, 0xff)(gen);
}

static void exit_handler() {
    if (timer != 0) {
        SDL_RemoveTimer(timer);
    }
    if (texture != nullptr) {
        SDL_DestroyTexture(texture);
    }
    if (renderer != nullptr) {
        SDL_DestroyRenderer(renderer);
    }
    if (window != nullptr) {
        SDL_DestroyWindow(window);
    }
    if (audio_dev != 0) {
        SDL_CloseAudioDevice(audio_dev);
    }
    SDL_Quit();
}

static void sdl_err(const char *const msg) {
    std::cerr << msg
              << ": "
              << SDL_GetError()
              << std::endl;
    std::exit(EXIT_FAILURE);
}

static void beep([[maybe_unused]] void *userdata, Uint8 *stream, int len) {
    for (int i = 0; i < len; i++) {
       stream[i] = i % 64;
    }
}

static Uint32 tick(Uint32 interval, [[maybe_unused]] void *param) {
    SDL_Event e {
	.user = SDL_UserEvent {
            .type = SDL_USEREVENT,
            .code = 0,
            .data1 = nullptr,
            .data2 = nullptr,
        },
    };
    if (SDL_PushEvent(&e) < 0) {
        sdl_err("Unable to push event");
    }
    return interval;
}

int main(int argc, char **argv) {
    std::atexit(exit_handler);

    if (argc != 2) {
        std::cerr << "Usage: "
                  << argv[0]
                  << " <file>"
                  << std::endl;
        return EXIT_FAILURE;
    }
    const char *app = argv[1];
    const uint8_t ms_per_tick = 17;
    const uint32_t insts_per_tick = 10;

    std::array<uint8_t, 0x1000> mem {
        #include "chars.h"
    };
    std::stack<uint16_t> stack;
    uint8_t V[0x10] {};
    uint16_t I = 0;
    uint8_t ST = 0;
    uint8_t DT = 0;
    uint16_t PC = 0x200;
    bool waiting_for_key = false;
    std::array<std::array<bool, 64>, 32> fb {};
    std::cout << "Initialized memory and registers" << std::endl;

    std::ifstream file(app, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Could not open file!" << std::endl;
	return EXIT_FAILURE;
    }
    file.read((char *)mem.data() + 0x200, 0xe00);
    file.close();

    // Init SDL
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO) != 0) {
        sdl_err("Unable to initialize SDL");
    }
    std::cout << "Initialized SDL" << std::endl;

    // Init audio
    {
        const SDL_AudioSpec want {
            .freq = 16000,
            .format = AUDIO_S8,
            .channels = 1,
            .silence = 0,
            .samples = 1024,
            .size = 0,
            .callback = beep, // we're beeping instead of writing silence
            .userdata = &audio_spec,
        };
        audio_dev = SDL_OpenAudioDevice(
            nullptr,
            0,
            &want,
            &audio_spec,
            SDL_AUDIO_ALLOW_ANY_CHANGE
        );
    }
    if (audio_dev == 0) {
        sdl_err("Could not open audio device");
    }
    std::cout << "Opened audio device " << audio_dev << std::endl;
    std::cout << "  freq: " << uint32_t(audio_spec.freq) << std::endl;
    std::cout << "  format: " << uint32_t(audio_spec.format) << std::endl;
    std::cout << "  channels: " << uint32_t(audio_spec.channels) << std::endl;
    std::cout << "  samples: " << uint32_t(audio_spec.samples) << std::endl;

    // Init window
    window = SDL_CreateWindow(
        "CHIP-8",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        100,
        100,
        SDL_WINDOW_RESIZABLE
    );
    if (window == nullptr) {
        sdl_err("Could not create window");
    }
    std::cout << "Opened window" << std::endl;

    // Init renderer and texture
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == nullptr) {
        sdl_err("Could not create renderer");
    }
    if (SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0x00) < 0) {
        sdl_err("Could not set draw color");
    }
    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        64,
        32
    );
    if (texture == nullptr) {
        sdl_err("Could not create texture");
    }
    if (SDL_RenderClear(renderer) < 0) {
        sdl_err("Could not clear rendering target");
    }

    // Init timer
    timer = SDL_AddTimer(ms_per_tick, tick, nullptr);
    if (timer == 0) {
        sdl_err("Could not add timer");
    }
    std::cout << "Initialized timer with "
              << uint32_t(ms_per_tick)
              << "ms per tick"
              << std::endl;

    const auto advance = [&PC]{ PC = (PC + 2) & 0xfff; };
    const auto advance2 = [&PC]{ PC = (PC + 4) & 0xfff; };
    // main loop
    for (;;) {
        SDL_Event e;
        if (SDL_WaitEvent(&e) == 0) {
            std::cerr << "Error while waiting for event: "
                << SDL_GetError()
                << std::endl;
            return EXIT_FAILURE;
        }
        switch (e.type) {
        case SDL_QUIT:
            std::cout << "Goodbye!" << std::endl;
            return EXIT_SUCCESS;
        case SDL_USEREVENT:
            if (DT != 0) {
                DT--;
            }

            if (ST != 0) {
                ST--;
                if (ST == 0) {
                    std::cout << "Beep :)" << std::endl;
                    SDL_PauseAudioDevice(audio_dev, 0);
                }
            } else {
                SDL_PauseAudioDevice(audio_dev, 1);
            }

            if (waiting_for_key) {
                continue;
            }
            for (size_t n = 0; n < insts_per_tick; n++) {
                const uint16_t op = (mem[PC] << 8) + mem[(PC + 1) & 0xfff];
                if (op == 0x00e0) {
                    for (size_t row = 0; row < fb.size(); row++) {
                        for (size_t col = 0; col < fb[row].size(); col++) {
                            fb[row][col] = 0;
                        }
                    }
                    advance();
                } else if (op == 0x00ee) {
                    PC = stack.top();
                    stack.pop();
                } else if ((op & 0xf000) == 0x1000) {
                    const uint16_t addr = op & 0xfff;
                    PC = addr;
                } else if ((op & 0xf000) == 0x2000) {
                    advance();
                    stack.push(PC);
                    const uint16_t addr = op & 0xfff;
                    PC = addr;
                } else if ((op & 0xf000) == 0x3000) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t byte = op & 0xff;
                    if (V[x] == byte) {
                        advance2();
                    } else {
                        advance();
                    }
                } else if ((op & 0xf000) == 0x4000) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t byte = op & 0xff;
                    if (V[x] != byte) {
                        advance2();
                    } else {
                        advance();
                    }
                } else if ((op & 0xf00f) == 0x5000) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t y = (op & 0xf0) >> 4;
                    if (V[x] == V[y]) {
                        advance2();
                    } else {
                        advance();
                    }
                } else if ((op & 0xf000) == 0x6000) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t byte = op & 0xff;
                    V[x] = byte;
                    advance();
                } else if ((op & 0xf000) == 0x7000) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t byte = op & 0xff;
                    V[x] += byte;
                    advance();
                } else if ((op & 0xf00f) == 0x8000) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t y = (op & 0xf0) >> 4;
                    V[x] = V[y];
                    advance();
                } else if ((op & 0xf00f) == 0x8001) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t y = (op & 0xf0) >> 4;
                    V[x] |= V[y];
                    advance();
                } else if ((op & 0xf00f) == 0x8002) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t y = (op & 0xf0) >> 4;
                    V[x] &= V[y];
                    advance();
                } else if ((op & 0xf00f) == 0x8003) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t y = (op & 0xf0) >> 4;
                    V[x] ^= V[y];
                    advance();
                } else if ((op & 0xf00f) == 0x8004) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t y = (op & 0xf0) >> 4;
                    V[x] += V[y];
                    V[0xf] = V[x] < V[y];
                    advance();
                } else if ((op & 0xf00f) == 0x8005) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t y = (op & 0xf0) >> 4;
                    V[0xf] = V[x] > V[y];
                    V[x] -= V[y];
                    advance();
                } else if ((op & 0xf00f) == 0x8006) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    V[0xf] = V[x] & 1;
                    V[x] >>= 1;
                    advance();
                } else if ((op & 0xf00f) == 0x8007) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t y = (op & 0xf0) >> 4;
                    V[0xf] = V[y] > V[x];
                    V[x] = V[y] - V[x];
                    advance();
                } else if ((op & 0xf00f) == 0x800e) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    V[0xf] = V[x] >> 7;
                    V[x] <<= 1;
                    advance();
                } else if ((op & 0xf00f) == 0x9000) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t y = (op & 0xf0) >> 4;
                    if (V[x] != V[y]) {
                        advance2();
                    } else {
                        advance();
                    }
                } else if ((op & 0xf000) == 0xa000) {
                    const uint16_t addr = op & 0xfff;
                    I = addr;
                    advance();
                } else if ((op & 0xf000) == 0xb000) {
                    const uint16_t addr = op & 0xfff;
                    PC = (V[0] + addr) & 0xfff;
                } else if ((op & 0xf000) == 0xc000) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t byte = op & 0xff;
                    V[x] = rnd() & byte;
                    advance();
                } else if ((op & 0xf000) == 0xd000) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const uint8_t y = (op & 0xf0) >> 4;
                    const uint8_t nibble = op & 0xf;
                    V[0xf] = 0;
                    for (size_t n = 0; n < nibble; n++) {
                        const uint8_t row = mem[(I + n) & 0xfff];
                        for (size_t b = 0; b < 8; b++) {
                            const bool sp_px = ((row >> (7 - b)) & 1) != 0;
                            bool &fb_px = fb[(n + V[y]) % 32][(b + V[x]) % 64];
                            if (sp_px && fb_px) {
                                V[0xf] = 1;
                            }
                            fb_px ^= sp_px;
                        }
                    }
                    advance();
                } else if ((op & 0xf0ff) == 0xe09e) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const Uint8 *const state = SDL_GetKeyboardState(NULL);
                    if (state[keys[V[x]]] == 1) {
                        advance2();
                    } else {
                        advance();
                    }
                } else if ((op & 0xf0ff) == 0xe0a1) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    const Uint8 *const state = SDL_GetKeyboardState(NULL);
                    if (state[keys[V[x]]] == 0) {
                        advance2();
                    } else {
                        advance();
                    }
                } else if ((op & 0xf0ff) == 0xf007) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    V[x] = DT;
                    advance();
                } else if ((op & 0xf0ff) == 0xf00a) {
                    waiting_for_key = true;
                    break;
                } else if ((op & 0xf0ff) == 0xf015) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    DT = V[x];
                    advance();
                } else if ((op & 0xf0ff) == 0xf018) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    ST = V[x];
                    advance();
                } else if ((op & 0xf0ff) == 0xf01e) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    I = (I + V[x]) & 0xfff;
                    advance();
                } else if ((op & 0xf0ff) == 0xf029) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    I = (V[x] * 5) & 0xfff;
                    advance();
                } else if ((op & 0xf0ff) == 0xf033) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    mem[I] = V[x] / 100;
                    mem[(I + 1) & 0xfff] = (V[x] % 100) / 10;
                    mem[(I + 2) & 0xfff] = V[x] % 10;
                    advance();
                } else if ((op & 0xf0ff) == 0xf055) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    for (size_t i = 0; i <= x; i++) {
                        mem[(I + i) & 0xfff] = V[i];
                    }
                    advance();
                } else if ((op & 0xf0ff) == 0xf065) {
                    const uint8_t x = (op & 0xf00) >> 8;
                    for (size_t i = 0; i <= x; i++) {
                        V[i] = mem[(I + i) & 0xfff];
                    }
                    advance();
                } else {
                    advance();
                }
            }

            Uint8 *px;
            int pitch;
            if (SDL_LockTexture(
                texture,
                nullptr,
                (void **)&px,
                &pitch
            ) < 0) {
                sdl_err("Could not lock texture");
            }
            for (size_t row = 0; row < fb.size(); row++) {
                for (size_t col = 0; col < fb[row].size(); col++) {
                    if (fb[row][col] != 0) {
                        px[row * pitch + col * 4] = 0xff;
                        px[row * pitch + col * 4 + 1] = 0xff;
                        px[row * pitch + col * 4 + 2] = 0xff;
                        px[row * pitch + col * 4 + 3] = 0xff;
                    } else {
                        px[row * pitch + col * 4] = 0;
                        px[row * pitch + col * 4 + 1] = 0;
                        px[row * pitch + col * 4 + 2] = 0;
                        px[row * pitch + col * 4 + 3] = 0;
                    }
                }
            }

            SDL_UnlockTexture(texture);

            if (SDL_RenderCopy(renderer, texture, NULL, NULL) < 0) {
                sdl_err("Could copy to rendering target");
            }
            SDL_RenderPresent(renderer);
            break;
        case SDL_WINDOWEVENT:
            if (SDL_RenderCopy(renderer, texture, NULL, NULL) < 0) {
                sdl_err("Could copy to rendering target");
            }
            SDL_RenderPresent(renderer);
            break;
        case SDL_KEYUP:
            if (waiting_for_key) {
                const auto key = std::find(
                    keys.begin(),
                    keys.end(),
                    e.key.keysym.scancode
                );
                if (key != keys.end()) {
                    const uint8_t x = mem[PC] & 0xf;
                    V[x] = std::distance(keys.begin(), key);
                    waiting_for_key = false;
                    advance();
                }
            }
            break;
        default:
            // ignore all other events
            break;
        }
    }
}
