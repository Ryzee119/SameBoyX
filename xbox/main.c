#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <hal/video.h>
#include <windows.h>
#include <SDL.h>
#include "Core/gb.h"
#include "utils.h"
#include "gui.h"
#include "audio/audio.h"

GB_gameboy_t gb;
static bool paused = false;
static uint32_t active_pixel_buffer[256 * 224];
static bool underclock_down = false, rewind_down = false, do_rewind = false, rewind_paused = false, turbo_down = false;
static double clock_mutliplier = 1.0;

static char *filename = NULL;
static typeof(free) *free_function = NULL;
static char *battery_save_path_ptr;

void set_filename(const char *new_filename, typeof(free) *new_free_function)
{
    if (filename && free_function)
    {
        free_function(filename);
    }
    filename = (char *)new_filename;
    free_function = new_free_function;
}

static void update_palette(void)
{
    switch (configuration.dmg_palette)
    {
    case 1: GB_set_palette(&gb, &GB_PALETTE_DMG); break;
    case 2: GB_set_palette(&gb, &GB_PALETTE_MGB); break;
    case 3: GB_set_palette(&gb, &GB_PALETTE_GBL); break;
    default: GB_set_palette(&gb, &GB_PALETTE_GREY);
    }
}

static void screen_size_changed(void)
{
    SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_STREAMING,
                                GB_get_screen_width(&gb), GB_get_screen_height(&gb));

    SDL_SetWindowMinimumSize(window, GB_get_screen_width(&gb), GB_get_screen_height(&gb));

    update_viewport();
}

static void open_menu(void)
{
    bool audio_playing = GB_audio_is_playing();
    if (audio_playing)
    {
        GB_audio_set_paused(true);
    }
    size_t previous_width = GB_get_screen_width(&gb);
    run_gui(true);
    SDL_ShowCursor(SDL_DISABLE);
    if (audio_playing)
    {
        GB_audio_set_paused(false);
    }
    GB_set_color_correction_mode(&gb, configuration.color_correction_mode);
    GB_set_border_mode(&gb, configuration.border_mode);
    update_palette();
    GB_set_highpass_filter_mode(&gb, configuration.highpass_mode);
    if (previous_width != GB_get_screen_width(&gb))
    {
        screen_size_changed();
    }
}

static void handle_events(GB_gameboy_t *gb)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            pending_command = GB_SDL_QUIT_COMMAND;
            break;

        case SDL_JOYBUTTONUP:
        case SDL_JOYBUTTONDOWN:
        {
            joypad_button_t button = get_joypad_button(event.jbutton.button);
            if ((GB_key_t)button < GB_KEY_MAX)
            {
                GB_set_key_state(gb, (GB_key_t)button, event.type == SDL_JOYBUTTONDOWN);
            }
            else if (button == JOYPAD_BUTTON_TURBO)
            {
                GB_audio_clear_queue();
                turbo_down = event.type == SDL_JOYBUTTONDOWN;
                GB_set_turbo_mode(gb, turbo_down, turbo_down && rewind_down);
            }
            else if (button == JOYPAD_BUTTON_SLOW_MOTION)
            {
                underclock_down = event.type == SDL_JOYBUTTONDOWN;
            }
            else if (button == JOYPAD_BUTTON_REWIND)
            {
                rewind_down = event.type == SDL_JOYBUTTONDOWN;
                if (event.type == SDL_JOYBUTTONUP)
                {
                    rewind_paused = false;
                }
                GB_set_turbo_mode(gb, turbo_down, turbo_down && rewind_down);
            }
            else if (button == JOYPAD_BUTTON_MENU && event.type == SDL_JOYBUTTONDOWN)
            {
                open_menu();
            }
        }
        break;

        case SDL_JOYAXISMOTION:
        {
            static bool axis_active[2] = {false, false};
            joypad_axis_t axis = get_joypad_axis(event.jaxis.axis);
            if (axis == JOYPAD_AXISES_X)
            {
                if (event.jaxis.value > JOYSTICK_HIGH)
                {
                    axis_active[0] = true;
                    GB_set_key_state(gb, GB_KEY_RIGHT, true);
                    GB_set_key_state(gb, GB_KEY_LEFT, false);
                }
                else if (event.jaxis.value < -JOYSTICK_HIGH)
                {
                    axis_active[0] = true;
                    GB_set_key_state(gb, GB_KEY_RIGHT, false);
                    GB_set_key_state(gb, GB_KEY_LEFT, true);
                }
                else if (axis_active[0] && event.jaxis.value < JOYSTICK_LOW && event.jaxis.value > -JOYSTICK_LOW)
                {
                    axis_active[0] = false;
                    GB_set_key_state(gb, GB_KEY_RIGHT, false);
                    GB_set_key_state(gb, GB_KEY_LEFT, false);
                }
            }
            else if (axis == JOYPAD_AXISES_Y)
            {
                if (event.jaxis.value > JOYSTICK_HIGH)
                {
                    axis_active[1] = true;
                    GB_set_key_state(gb, GB_KEY_DOWN, true);
                    GB_set_key_state(gb, GB_KEY_UP, false);
                }
                else if (event.jaxis.value < -JOYSTICK_HIGH)
                {
                    axis_active[1] = true;
                    GB_set_key_state(gb, GB_KEY_DOWN, false);
                    GB_set_key_state(gb, GB_KEY_UP, true);
                }
                else if (axis_active[1] && event.jaxis.value < JOYSTICK_LOW && event.jaxis.value > -JOYSTICK_LOW)
                {
                    axis_active[1] = false;
                    GB_set_key_state(gb, GB_KEY_DOWN, false);
                    GB_set_key_state(gb, GB_KEY_UP, false);
                }
            }
        }
        break;

        case SDL_JOYHATMOTION:
        {
            uint8_t value = event.jhat.value;
            int8_t updown =
                value == SDL_HAT_LEFTUP || value == SDL_HAT_UP || value == SDL_HAT_RIGHTUP ? -1 : (value == SDL_HAT_LEFTDOWN || value == SDL_HAT_DOWN || value == SDL_HAT_RIGHTDOWN ? 1 : 0);
            int8_t leftright =
                value == SDL_HAT_LEFTUP || value == SDL_HAT_LEFT || value == SDL_HAT_LEFTDOWN ? -1 : (value == SDL_HAT_RIGHTUP || value == SDL_HAT_RIGHT || value == SDL_HAT_RIGHTDOWN ? 1 : 0);

            GB_set_key_state(gb, GB_KEY_LEFT, leftright == -1);
            GB_set_key_state(gb, GB_KEY_RIGHT, leftright == 1);
            GB_set_key_state(gb, GB_KEY_UP, updown == -1);
            GB_set_key_state(gb, GB_KEY_DOWN, updown == 1);
            break;
        };
        default:
            break;
        }
    }
}

static void vblank(GB_gameboy_t *gb)
{
    handle_events(gb);

    static int frame_skip = 0;
    if (frame_skip++ % 2)
        return;

    if (underclock_down && clock_mutliplier > 0.5)
    {
        clock_mutliplier -= 1.0 / 16;
        GB_set_clock_multiplier(gb, clock_mutliplier);
    }
    else if (!underclock_down && clock_mutliplier < 1.0)
    {
        clock_mutliplier += 1.0 / 16;
        GB_set_clock_multiplier(gb, clock_mutliplier);
    }

    render_texture(active_pixel_buffer, NULL);
    do_rewind = rewind_down;
}

static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return SDL_MapRGB(pixel_format, r, g, b);
}

static void rumble(GB_gameboy_t *gb, double amp)
{
    SDL_HapticRumblePlay(haptic, amp, 250);
}

static void gb_audio_callback(GB_gameboy_t *gb, GB_sample_t *sample)
{
    if (turbo_down)
    {
        static unsigned skip = 0;
        skip++;
        if (skip == GB_audio_get_frequency() / 8)
        {
            skip = 0;
        }
        if (skip > GB_audio_get_frequency() / 16)
        {
            return;
        }
    }

    if (GB_audio_get_queue_length() / sizeof(*sample) > GB_audio_get_frequency() / 4)
    {
        return;
    }

    if (configuration.volume != 100)
    {
        sample->left = sample->left * configuration.volume / 100;
        sample->right = sample->right * configuration.volume / 100;
    }

    GB_audio_queue_sample(sample);
}

static bool handle_pending_command(void)
{
    switch (pending_command)
    {
    case GB_SDL_LOAD_STATE_COMMAND:
    case GB_SDL_SAVE_STATE_COMMAND:
    {
        char save_path[strlen(filename) + 4];
        char save_extension[] = ".s0";
        save_extension[2] += command_parameter;
        replace_extension(filename, strlen(filename), save_path, save_extension);

        if (pending_command == GB_SDL_LOAD_STATE_COMMAND)
        {
            GB_load_state(&gb, save_path);
        }
        else
        {
            GB_save_state(&gb, save_path);
        }
        return false;
    }

    case GB_SDL_NO_COMMAND:
        return false;

    case GB_SDL_RESET_COMMAND:
    case GB_SDL_NEW_FILE_COMMAND:
        GB_save_battery(&gb, battery_save_path_ptr);
        return true;

    case GB_SDL_QUIT_COMMAND:
        GB_save_battery(&gb, battery_save_path_ptr);
        exit(0);
    }
    return false;
}

static void load_boot_rom(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    static const char *const names[] = {
        [GB_BOOT_ROM_DMG0] = "dmg0_boot.bin",
        [GB_BOOT_ROM_DMG] = "dmg_boot.bin",
        [GB_BOOT_ROM_MGB] = "mgb_boot.bin",
        [GB_BOOT_ROM_SGB] = "sgb_boot.bin",
        [GB_BOOT_ROM_SGB2] = "sgb2_boot.bin",
        [GB_BOOT_ROM_CGB0] = "cgb0_boot.bin",
        [GB_BOOT_ROM_CGB] = "cgb_boot.bin",
        [GB_BOOT_ROM_AGB] = "agb_boot.bin",
    };
    GB_load_boot_rom(gb, resource_path(names[type]));
}

static void run(void)
{
    SDL_ShowCursor(SDL_DISABLE);
    GB_model_t model;
    pending_command = GB_SDL_NO_COMMAND;
restart:
    model = (GB_model_t[]){
        [MODEL_DMG] = GB_MODEL_DMG_B,
        [MODEL_CGB] = GB_MODEL_CGB_E,
        [MODEL_AGB] = GB_MODEL_AGB,
        [MODEL_SGB] = (GB_model_t[]){
            [SGB_NTSC] = GB_MODEL_SGB_NTSC,
            [SGB_PAL] = GB_MODEL_SGB_PAL,
            [SGB_2] = GB_MODEL_SGB2,
        }[configuration.sgb_revision],
    }[configuration.model];

    if (GB_is_inited(&gb))
    {
        GB_switch_model_and_reset(&gb, model);
    }
    else
    {
        GB_init(&gb, model);

        GB_set_boot_rom_load_callback(&gb, load_boot_rom);
        GB_set_vblank_callback(&gb, (GB_vblank_callback_t)vblank);
        GB_set_pixels_output(&gb, active_pixel_buffer);
        GB_set_rgb_encode_callback(&gb, rgb_encode);
        GB_set_rumble_callback(&gb, rumble);
        GB_set_rumble_mode(&gb, configuration.rumble_mode);
        GB_set_sample_rate(&gb, GB_audio_get_frequency());
        GB_set_color_correction_mode(&gb, configuration.color_correction_mode);
        update_palette();
        if ((unsigned)configuration.border_mode <= GB_BORDER_ALWAYS)
        {
            GB_set_border_mode(&gb, configuration.border_mode);
        }
        GB_set_highpass_filter_mode(&gb, configuration.highpass_mode);
        GB_set_rewind_length(&gb, configuration.rewind_length);
        GB_set_update_input_hint_callback(&gb, handle_events);
        GB_apu_set_sample_callback(&gb, gb_audio_callback);
    }

    bool error = false;
    size_t path_length = strlen(filename);
    char extension[4] = {
        0,
    };
    if (path_length > 4)
    {
        if (filename[path_length - 4] == '.')
        {
            extension[0] = tolower(filename[path_length - 3]);
            extension[1] = tolower(filename[path_length - 2]);
            extension[2] = tolower(filename[path_length - 1]);
        }
    }
    if (strcmp(extension, "isx") == 0)
    {
        error = GB_load_isx(&gb, filename);
        /* Configure battery */
        char battery_save_path[path_length + 5]; /* At the worst case, size is strlen(path) + 4 bytes for .sav + NULL */
        replace_extension(filename, path_length, battery_save_path, ".ram");
        battery_save_path_ptr = battery_save_path;
        GB_load_battery(&gb, battery_save_path);
    }
    else
    {
        GB_load_rom(&gb, filename);
    }

    /* Configure battery */
    char battery_save_path[path_length + 5]; /* At the worst case, size is strlen(path) + 4 bytes for .sav + NULL */
    replace_extension(filename, path_length, battery_save_path, ".sav");
    battery_save_path_ptr = battery_save_path;
    GB_load_battery(&gb, battery_save_path);

    screen_size_changed();

    /* Run emulation */
    while (true)
    {
        if (paused || rewind_paused)
        {
            SDL_WaitEvent(NULL);
            handle_events(&gb);
        }
        else
        {
            if (do_rewind)
            {
                GB_rewind_pop(&gb);
                if (turbo_down)
                {
                    GB_rewind_pop(&gb);
                }
                if (!GB_rewind_pop(&gb))
                {
                    rewind_paused = true;
                }
                do_rewind = false;
            }
            GB_run(&gb);
        }

        /* These commands can't run in the handle_event function, because they're not safe in a vblank context. */
        if (handle_pending_command())
        {
            pending_command = GB_SDL_NO_COMMAND;
            goto restart;
        }
        pending_command = GB_SDL_NO_COMMAND;
    }
}

static char prefs_path[1024] = {
    0,
};

static void save_configuration(void)
{
    FILE *prefs_file = fopen(prefs_path, "wb");
    if (prefs_file)
    {
        fwrite(&configuration, 1, sizeof(configuration), prefs_file);
        fclose(prefs_file);
    }
}

int main(int argc, char **argv)
{
    #define str(x) #x
    #define xstr(x) str(x)
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

    bool fullscreen = true;
    filename = malloc(256);
    strcpy(filename, "D:\\test.gb");

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    strcpy(prefs_path, resource_path("prefs.bin"));
    FILE *prefs_file = fopen(prefs_path, "rb");
    if (prefs_file)
    {
        fread(&configuration, 1, sizeof(configuration), prefs_file);
        fclose(prefs_file);

        /* Sanitize for stability */
        configuration.color_correction_mode %= GB_COLOR_CORRECTION_REDUCE_CONTRAST + 1;
        configuration.scaling_mode %= GB_SDL_SCALING_MAX;
        configuration.default_scale %= GB_SDL_DEFAULT_SCALE_MAX + 1;
        configuration.highpass_mode %= GB_HIGHPASS_MAX;
        configuration.model %= MODEL_MAX;
        configuration.sgb_revision %= SGB_MAX;
        configuration.dmg_palette %= 3;
        configuration.border_mode %= GB_BORDER_ALWAYS + 1;
        configuration.rumble_mode %= GB_RUMBLE_ALL_GAMES + 1;
    }

    if (configuration.model >= MODEL_MAX)
    {
        configuration.model = MODEL_CGB;
    }

    if (configuration.default_scale == 0)
    {
        configuration.default_scale = 2;
    }
    configuration.blending_mode = 0;
    atexit(save_configuration);

    window = SDL_CreateWindow("SameBoy v" xstr(VERSION), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              160 * configuration.default_scale, 144 * configuration.default_scale, SDL_WINDOW_FULLSCREEN);

    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    texture = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_STREAMING, 160, 144);
    pixel_format = SDL_AllocFormat(SDL_GetWindowPixelFormat(window));

    GB_audio_init();

    if (filename == NULL)
        run_gui(false);
    else
        connect_joypad();

    GB_audio_set_paused(false);
    run(); // Never returns
    return 0;
}
