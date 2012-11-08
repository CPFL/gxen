/*
 * QEMU SDL display driver
 * 
 * Copyright (c) 2003 Fabrice Bellard
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu-common.h"
#include "console.h"
#include "sysemu.h"
#include "x_keymap.h"

#include <SDL.h>
#include <SDL_syswm.h>

#ifndef _WIN32
#include <signal.h>
#endif

#ifdef CONFIG_OPENGL
#include <SDL_opengl.h>
#endif

static DisplayChangeListener *dcl;
static SDL_Surface *real_screen;
static SDL_Surface *guest_screen = NULL;
static int gui_grab; /* if true, all keyboard/mouse events are grabbed */
static int last_vm_running;
static int gui_saved_grab;
static int gui_fullscreen;
static int gui_noframe;
static int gui_key_modifier_pressed;
static int gui_keysym;
static int gui_fullscreen_initial_grab;
static int gui_grab_code = KMOD_LALT | KMOD_LCTRL;
static uint8_t modifiers_state[256];
static int width, height;
static SDL_Cursor *sdl_cursor_normal;
static SDL_Cursor *sdl_cursor_hidden;
static int absolute_enabled = 0;
static int opengl_enabled;
static uint8_t allocator;
static uint8_t hostbpp;

#ifdef CONFIG_OPENGL
static GLint tex_format;
static GLint tex_type;
static GLuint texture_ref = 0;
static GLint gl_format;
static uint8_t bgr;

static void opengl_setdata(DisplayState *ds)
{
    glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glClearColor(0, 0, 0, 0);
    glDisable(GL_BLEND);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glViewport( 0, 0, real_screen->w, real_screen->h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, real_screen->w, real_screen->h, 0, -1,1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClear(GL_COLOR_BUFFER_BIT);

    if (texture_ref) {
        glDeleteTextures(1, &texture_ref);
        texture_ref = 0;
    }

    glGenTextures(1, &texture_ref);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, texture_ref);
    glPixelStorei(GL_UNPACK_LSB_FIRST, 1);
    switch (ds_get_bits_per_pixel(ds)) {
        case 16:
            tex_format = GL_RGB;
            tex_type = GL_UNSIGNED_SHORT_5_6_5;
            break;
        case 24:
            tex_format = GL_BGR;
            tex_type = GL_UNSIGNED_BYTE;
            break;
        case 32:
            if (bgr == (ds->surface->pf.rshift < ds->surface->pf.bshift)) {
                tex_format = GL_BGRA;
                tex_type = GL_UNSIGNED_BYTE;
            } else {
                tex_format = GL_RGBA;
                tex_type = GL_UNSIGNED_BYTE;                
            }
            break;
    }   
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (ds_get_linesize(ds) / ds_get_bytes_per_pixel(ds)));
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, gl_format, ds_get_width(ds), ds_get_height(ds), 0, tex_format, tex_type, ds_get_data(ds));
    glTexParameterf(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_PRIORITY, 1.0);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
}

static void opengl_update(DisplayState *ds, int x, int y, int w, int h)
{  
    int bpp = ds_get_bytes_per_pixel(ds);
    GLvoid *pixels = ds_get_data(ds) + y * ds_get_linesize(ds) + x * bpp;
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, texture_ref);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, ds_get_linesize(ds) / bpp);
    glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, x, y, w, h, tex_format, tex_type, pixels);
    glBegin(GL_QUADS);
        glTexCoord2d(0, 0);
        glVertex2d(0, 0);
        glTexCoord2d(ds_get_width(ds), 0);
        glVertex2d(real_screen->w, 0);
        glTexCoord2d(ds_get_width(ds), ds_get_height(ds));
        glVertex2d(real_screen->w, real_screen->h);
        glTexCoord2d(0, ds_get_height(ds));
        glVertex2d(0, real_screen->h);
    glEnd();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
    SDL_GL_SwapBuffers();
}
#endif

static void sdl_update(DisplayState *ds, int x, int y, int w, int h)
{
    //    printf("updating x=%d y=%d w=%d h=%d\n", x, y, w, h);i
    if (guest_screen) {
        SDL_Rect rec;
        rec.x = x;
        rec.y = y;
        rec.w = w;
        rec.h = h;
        SDL_BlitSurface(guest_screen, &rec, real_screen, &rec);
    }
    SDL_UpdateRect(real_screen, x, y, w, h);
}

static void sdl_setdata(DisplayState *ds)
{
    SDL_Rect rec;
    rec.x = 0;
    rec.y = 0;
    rec.w = real_screen->w;
    rec.h = real_screen->h;

    if (guest_screen != NULL) SDL_FreeSurface(guest_screen);

    guest_screen = SDL_CreateRGBSurfaceFrom(ds_get_data(ds), ds_get_width(ds), ds_get_height(ds),
                                            ds_get_bits_per_pixel(ds), ds_get_linesize(ds),
                                            ds->surface->pf.rmask, ds->surface->pf.gmask,
                                            ds->surface->pf.bmask, ds->surface->pf.amask);
}

static void do_sdl_resize(int new_width, int new_height, int bpp)
{
    int flags;

    //    printf("resizing to %d %d\n", w, h);
#ifdef CONFIG_OPENGL
    if (opengl_enabled)
        flags = SDL_OPENGL|SDL_RESIZABLE;
    else
#endif
        flags = SDL_HWSURFACE|SDL_ASYNCBLIT|SDL_HWACCEL;

    if (gui_fullscreen)
        flags |= SDL_FULLSCREEN;
    if (gui_noframe)
        flags |= SDL_NOFRAME;

    width = new_width;
    height = new_height;
    real_screen = SDL_SetVideoMode(width, height, bpp, flags);
    if (!real_screen) {
        if (opengl_enabled) {
            /* Fallback to SDL */
            opengl_enabled = 0;
            dcl->dpy_update = sdl_update;
            dcl->dpy_setdata = sdl_setdata;
            do_sdl_resize(width, height, bpp);
            return;
        }
        fprintf(stderr, "Could not open SDL display\n");
        exit(1);
    }

#ifdef CONFIG_OPENGL
    if (real_screen->format->Bshift > real_screen->format->Rshift) {
        bgr = 1;
    } else {
        bgr = 0;
    }
    switch(real_screen->format->BitsPerPixel) {
        case 8:
            gl_format = GL_RGB;
            break;
        case 16:
            gl_format = GL_RGB;
            break;
        case 24:
            gl_format = GL_RGB;
            break;
        case 32:
            if (!real_screen->format->Rshift)
                gl_format = GL_BGRA;
            else
                gl_format = GL_RGBA;
            break;
    };
#endif
}

static void sdl_resize(DisplayState *ds)
{
    if  (!allocator) {
        do_sdl_resize(ds_get_width(ds), ds_get_height(ds), 0);
        dcl->dpy_setdata(ds);
    } else {
        if (guest_screen != NULL) {
            SDL_FreeSurface(guest_screen);
            guest_screen = NULL;
        }
    }
}

static PixelFormat sdl_to_qemu_pixelformat(SDL_PixelFormat *sdl_pf)
{
    PixelFormat qemu_pf;

    memset(&qemu_pf, 0x00, sizeof(PixelFormat));

    qemu_pf.bits_per_pixel = sdl_pf->BitsPerPixel;
    qemu_pf.bytes_per_pixel = sdl_pf->BytesPerPixel;
    qemu_pf.depth = (qemu_pf.bits_per_pixel) == 32 ? 24 : (qemu_pf.bits_per_pixel);

    qemu_pf.rmask = sdl_pf->Rmask;
    qemu_pf.gmask = sdl_pf->Gmask;
    qemu_pf.bmask = sdl_pf->Bmask;
    qemu_pf.amask = sdl_pf->Amask;

    qemu_pf.rshift = sdl_pf->Rshift;
    qemu_pf.gshift = sdl_pf->Gshift;
    qemu_pf.bshift = sdl_pf->Bshift;
    qemu_pf.ashift = sdl_pf->Ashift;

    qemu_pf.rbits = 8 - sdl_pf->Rloss;
    qemu_pf.gbits = 8 - sdl_pf->Gloss;
    qemu_pf.bbits = 8 - sdl_pf->Bloss;
    qemu_pf.abits = 8 - sdl_pf->Aloss;

    qemu_pf.rmax = ((1 << qemu_pf.rbits) - 1);
    qemu_pf.gmax = ((1 << qemu_pf.gbits) - 1);
    qemu_pf.bmax = ((1 << qemu_pf.bbits) - 1);
    qemu_pf.amax = ((1 << qemu_pf.abits) - 1);

    return qemu_pf;
}

static DisplaySurface* sdl_create_displaysurface(int width, int height)
{
    DisplaySurface *surface = (DisplaySurface*) qemu_mallocz(sizeof(DisplaySurface));
    if (surface == NULL) {
        fprintf(stderr, "sdl_create_displaysurface: malloc failed\n");
        exit(1);
    }

    surface->width = width;
    surface->height = height;

    if (hostbpp == 16)
        do_sdl_resize(width, height, 16);
    else
        do_sdl_resize(width, height, 32);

    surface->pf = sdl_to_qemu_pixelformat(real_screen->format);
    surface->linesize = real_screen->pitch;
    surface->data = real_screen->pixels;

#ifdef WORDS_BIGENDIAN
    surface->flags = QEMU_ALLOCATED_FLAG | QEMU_BIG_ENDIAN_FLAG;
#else
    surface->flags = QEMU_ALLOCATED_FLAG;
#endif
    allocator = 1;

    return surface;
}

static void sdl_free_displaysurface(DisplaySurface *surface)
{
    allocator = 0;
    if (surface == NULL)
        return;
    qemu_free(surface);
}

static DisplaySurface* sdl_resize_displaysurface(DisplaySurface *surface, int width, int height)
{
    sdl_free_displaysurface(surface);
    return sdl_create_displaysurface(width, height);
}

/* generic keyboard conversion */

#include "sdl_keysym.h"
#include "keymaps.c"

static kbd_layout_t *kbd_layout = NULL;

static uint8_t sdl_keyevent_to_keycode_generic(const SDL_KeyboardEvent *ev)
{
    int keysym;
    /* workaround for X11+SDL bug with AltGR */
    keysym = ev->keysym.sym;
    if (keysym == 0 && ev->keysym.scancode == 113)
        keysym = SDLK_MODE;
    /* For Japanese key '\' and '|' */
    if (keysym == 92 && ev->keysym.scancode == 133) {
        keysym = 0xa5;
    }
    return keysym2scancode(kbd_layout, keysym);
}

/* specific keyboard conversions from scan codes */

#if defined(_WIN32)

static uint8_t sdl_keyevent_to_keycode(const SDL_KeyboardEvent *ev)
{
    return ev->keysym.scancode;
}

#else

#if defined(SDL_VIDEO_DRIVER_X11)
#include <X11/XKBlib.h>

static int check_for_evdev(void)
{
    SDL_SysWMinfo info;
    XkbDescPtr desc;
    int has_evdev = 0;
    const char *keycodes;

    SDL_VERSION(&info.version);
    if (!SDL_GetWMInfo(&info))
        return 0;

    desc = XkbGetKeyboard(info.info.x11.display,
                          XkbGBN_AllComponentsMask,
                          XkbUseCoreKbd);
    if (desc == NULL || desc->names == NULL)
        return 0;

    keycodes = XGetAtomName(info.info.x11.display, desc->names->keycodes);
    if (keycodes == NULL)
        fprintf(stderr, "could not lookup keycode name\n");
    else if (strstart(keycodes, "evdev", NULL))
        has_evdev = 1;
    else if (!strstart(keycodes, "xfree86", NULL))
        fprintf(stderr,
                "unknown keycodes `%s', please report to qemu-devel@nongnu.org\n",
                keycodes);

    XkbFreeClientMap(desc, XkbGBN_AllComponentsMask, True);

    return has_evdev;
}
#else
static int check_for_evdev(void)
{
	return 0;
}
#endif

static uint8_t sdl_keyevent_to_keycode(const SDL_KeyboardEvent *ev)
{
    int keycode;
    static int has_evdev = -1;

    if (has_evdev == -1)
        has_evdev = check_for_evdev();

    keycode = ev->keysym.scancode;

    if (keycode < 9) {
        keycode = 0;
    } else if (keycode < 97) {
        keycode -= 8; /* just an offset */
    } else if (keycode < 158) {
        /* use conversion table */
        if (has_evdev)
            keycode = translate_evdev_keycode(keycode - 97);
        else
            keycode = translate_xfree86_keycode(keycode - 97);
    } else if (keycode == 208) { /* Hiragana_Katakana */
        keycode = 0x70;
    } else if (keycode == 211) { /* backslash */
        keycode = 0x73;
    } else {
        keycode = 0;
    }
    return keycode;
}

#endif

static void reset_keys(void)
{
    int i;
    for(i = 0; i < 256; i++) {
        if (modifiers_state[i]) {
            if (i & 0x80)
                kbd_put_keycode(0xe0);
            kbd_put_keycode(i | 0x80);
            modifiers_state[i] = 0;
        }
    }
}

static void sdl_process_key(SDL_KeyboardEvent *ev)
{
    int keycode, v;

    if (ev->keysym.sym == SDLK_PAUSE) {
        /* specific case */
        v = 0;
        if (ev->type == SDL_KEYUP)
            v |= 0x80;
        kbd_put_keycode(0xe1);
        kbd_put_keycode(0x1d | v);
        kbd_put_keycode(0x45 | v);
        return;
    }

    if (kbd_layout) {
        keycode = sdl_keyevent_to_keycode_generic(ev);
    } else {
        keycode = sdl_keyevent_to_keycode(ev);
    }

    switch(keycode) {
    case 0x00:
        /* sent when leaving window: reset the modifiers state */
        reset_keys();
        return;
    case 0x2a:                          /* Left Shift */
    case 0x36:                          /* Right Shift */
    case 0x1d:                          /* Left CTRL */
    case 0x9d:                          /* Right CTRL */
    case 0x38:                          /* Left ALT */
    case 0xb8:                         /* Right ALT */
        if (ev->type == SDL_KEYUP)
            modifiers_state[keycode] = 0;
        else
            modifiers_state[keycode] = 1;
        break;
    case 0x45: /* num lock */
    case 0x3a: /* caps lock */
        /* SDL does not send the key up event, so we generate it */
        kbd_put_keycode(keycode);
        kbd_put_keycode(keycode | 0x80);
        return;
    }

    /* now send the key code */
    if (keycode & 0x80)
        kbd_put_keycode(0xe0);
    if (ev->type == SDL_KEYUP)
        kbd_put_keycode(keycode | 0x80);
    else
        kbd_put_keycode(keycode & 0x7f);
}

static void sdl_update_caption(void)
{
    char buf[1024];
    strcpy(buf, domain_name);
    if (!vm_running) {
        strcat(buf, " [Stopped]");
    }
    if (gui_grab) {
        if (!alt_grab)
	    strcat(buf, " - Press Ctrl-Alt to exit grab");
	else
            strcat(buf, " - Press Ctrl-Alt-Shift to exit grab");
    }
    SDL_WM_SetCaption(buf, domain_name);
}

static void sdl_hide_cursor(void)
{
    if (!cursor_hide)
        return;

    if (kbd_mouse_is_absolute()) {
        SDL_ShowCursor(1);
        SDL_SetCursor(sdl_cursor_hidden);
    } else {
        SDL_ShowCursor(0);
    }
}

static void sdl_show_cursor(void)
{
    if (!cursor_hide)
        return;

    if (!kbd_mouse_is_absolute()) {
        SDL_ShowCursor(1);
        SDL_SetCursor(sdl_cursor_normal);
    }
}

static void sdl_grab_start(void)
{
    sdl_hide_cursor();
    SDL_WM_GrabInput(SDL_GRAB_ON);
    gui_grab = 1;
    sdl_update_caption();
}

static void sdl_grab_end(void)
{
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    gui_grab = 0;
    sdl_show_cursor();
    sdl_update_caption();
}

static void sdl_send_mouse_event(int dx, int dy, int dz, int state)
{
    int buttons = 0;
    if (state & SDL_BUTTON(SDL_BUTTON_LEFT))
        buttons |= MOUSE_EVENT_LBUTTON;
    if (state & SDL_BUTTON(SDL_BUTTON_RIGHT))
        buttons |= MOUSE_EVENT_RBUTTON;
    if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE))
        buttons |= MOUSE_EVENT_MBUTTON;

    if (kbd_mouse_is_absolute()) {
	if (!absolute_enabled) {
	    sdl_hide_cursor();
	    if (gui_grab) {
		sdl_grab_end();
	    }
	    absolute_enabled = 1;
	}

	SDL_GetMouseState(&dx, &dy);
        dx = dx * 0x7FFF / (real_screen->w - 1);
        dy = dy * 0x7FFF / (real_screen->h - 1);
    } else if (absolute_enabled) {
	sdl_show_cursor();
	absolute_enabled = 0;
    }

    kbd_mouse_event(dx, dy, dz, buttons);
}

static void toggle_full_screen(DisplayState *ds)
{
    gui_fullscreen = !gui_fullscreen;
    sdl_resize(ds);
    if (gui_fullscreen) {
        gui_saved_grab = gui_grab;
        sdl_grab_start();
    } else {
        if (!gui_saved_grab)
            sdl_grab_end();
    }
    vga_hw_invalidate();
    vga_hw_update();
}

static void sdl_refresh(DisplayState *ds)
{
    SDL_Event ev1, *ev = &ev1;
    int mod_state;
                     
    if (last_vm_running != vm_running) {
        last_vm_running = vm_running;
        sdl_update_caption();
    }

    vga_hw_update();
    SDL_EnableUNICODE(!is_graphic_console());

    while (SDL_PollEvent(ev)) {
        switch (ev->type) {
        case SDL_VIDEOEXPOSE:
            dcl->dpy_update(ds, 0, 0, ds_get_width(ds), ds_get_height(ds));
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (ev->type == SDL_KEYDOWN) {
                if (!alt_grab) {
		    mod_state = (SDL_GetModState() & gui_grab_code) ==
			gui_grab_code;
                } else {
                    mod_state = (SDL_GetModState() & (gui_grab_code | KMOD_LSHIFT)) ==
                                (gui_grab_code | KMOD_LSHIFT);
                }
                gui_key_modifier_pressed = mod_state;
                if (gui_key_modifier_pressed) {
                    int keycode;
                    keycode = sdl_keyevent_to_keycode(&ev->key);
                    switch(keycode) {
                    case 0x21: /* 'f' key on US keyboard */
                        toggle_full_screen(ds);
                        gui_keysym = 1;
                        break;
                    case 0x02 ... 0x0a: /* '1' to '9' keys */ 
                        /* Reset the modifiers sent to the current console */
                        reset_keys();
                        console_select(keycode - 0x02);
                        if (!is_graphic_console()) {
                            /* display grab if going to a text console */
                            if (gui_grab)
                                sdl_grab_end();
                        }
                        gui_keysym = 1;
                        break;
                    default:
                        break;
                    }
                } else if (!is_graphic_console()) {
                    int keysym;
                    keysym = 0;
                    if (ev->key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL)) {
                        switch(ev->key.keysym.sym) {
                        case SDLK_UP: keysym = QEMU_KEY_CTRL_UP; break;
                        case SDLK_DOWN: keysym = QEMU_KEY_CTRL_DOWN; break;
                        case SDLK_LEFT: keysym = QEMU_KEY_CTRL_LEFT; break;
                        case SDLK_RIGHT: keysym = QEMU_KEY_CTRL_RIGHT; break;
                        case SDLK_HOME: keysym = QEMU_KEY_CTRL_HOME; break;
                        case SDLK_END: keysym = QEMU_KEY_CTRL_END; break;
                        case SDLK_PAGEUP: keysym = QEMU_KEY_CTRL_PAGEUP; break;
                        case SDLK_PAGEDOWN: keysym = QEMU_KEY_CTRL_PAGEDOWN; break;
                        default: break;
                        }
                    } else {
                        switch(ev->key.keysym.sym) {
                        case SDLK_UP: keysym = QEMU_KEY_UP; break;
                        case SDLK_DOWN: keysym = QEMU_KEY_DOWN; break;
                        case SDLK_LEFT: keysym = QEMU_KEY_LEFT; break;
                        case SDLK_RIGHT: keysym = QEMU_KEY_RIGHT; break;
                        case SDLK_HOME: keysym = QEMU_KEY_HOME; break;
                        case SDLK_END: keysym = QEMU_KEY_END; break;
                        case SDLK_PAGEUP: keysym = QEMU_KEY_PAGEUP; break;
                        case SDLK_PAGEDOWN: keysym = QEMU_KEY_PAGEDOWN; break;
                        case SDLK_BACKSPACE: keysym = QEMU_KEY_BACKSPACE; break;
                        case SDLK_DELETE: keysym = QEMU_KEY_DELETE; break;
                        default: break;
                        }
                    }
                    if (keysym) {
                        kbd_put_keysym(keysym);
                    } else if (ev->key.keysym.unicode != 0) {
                        kbd_put_keysym(ev->key.keysym.unicode);
                    }
                }
            } else if (ev->type == SDL_KEYUP) {
                if (!alt_grab) {
		    mod_state = (ev->key.keysym.mod & gui_grab_code);
                } else {
                    mod_state = (ev->key.keysym.mod &
                                 (gui_grab_code | KMOD_LSHIFT));
                }
                if (!mod_state) {
                    if (gui_key_modifier_pressed) {
                        gui_key_modifier_pressed = 0;
                        if (gui_keysym == 0) {
                            /* exit/enter grab if pressing Ctrl-Alt */
                            if (!gui_grab) {
                                /* if the application is not active,
                                   do not try to enter grab state. It
                                   prevents
                                   'SDL_WM_GrabInput(SDL_GRAB_ON)'
                                   from blocking all the application
                                   (SDL bug). */
                                if (SDL_GetAppState() & SDL_APPACTIVE)
                                    sdl_grab_start();
                            } else {
                                sdl_grab_end();
                            }
                            /* SDL does not send back all the
                               modifiers key, so we must correct it */
                            reset_keys();
                            break;
                        }
                        gui_keysym = 0;
                    }
                }
            }
            if (is_graphic_console() && !gui_keysym) 
                sdl_process_key(&ev->key);
            break;
        case SDL_QUIT:
            if (!no_quit) {
               qemu_system_shutdown_request();
               vm_start();    /* In case we're paused */
            }
            break;
        case SDL_MOUSEMOTION:
            if (gui_grab || kbd_mouse_is_absolute() ||
                absolute_enabled) {
                int dx, dy, state;
                state = SDL_GetRelativeMouseState(&dx, &dy);
                if (dx || dy)
                    sdl_send_mouse_event(dx, dy, 0, state);
            }
            break;
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEBUTTONDOWN:
            {
                SDL_MouseButtonEvent *bev = &ev->button;
                if (!gui_grab && !kbd_mouse_is_absolute()) {
                    if (ev->type == SDL_MOUSEBUTTONDOWN &&
                        (bev->button == SDL_BUTTON_LEFT)) {
                        /* start grabbing all events */
                        sdl_grab_start();
                    }
                } else {
                    int dz, state;
                    dz = 0;
                    state = SDL_GetMouseState(NULL, NULL);
                    if (ev->type == SDL_MOUSEBUTTONDOWN) {
                        state |= SDL_BUTTON(bev->button);
                    } else {
                        state &= ~SDL_BUTTON(bev->button);
                    }
#ifdef SDL_BUTTON_WHEELUP
                    if (bev->button == SDL_BUTTON_WHEELUP && ev->type == SDL_MOUSEBUTTONDOWN) {
                        dz = -1;
                    } else if (bev->button == SDL_BUTTON_WHEELDOWN && ev->type == SDL_MOUSEBUTTONDOWN) {
                        dz = 1;
                    }
#endif
                    sdl_send_mouse_event(0, 0, dz, state);
                }
            }
            break;
        case SDL_ACTIVEEVENT:
            if (gui_grab && ev->active.state == SDL_APPINPUTFOCUS &&
                !ev->active.gain && !gui_fullscreen_initial_grab) {
                sdl_grab_end();
            }
	    if (ev->active.state & SDL_APPACTIVE) {
		if (ev->active.gain) {
		    /* Back to default interval */
		    dcl->gui_timer_interval = 0;
		    dcl->idle = 0;
		} else {
		    /* Sleeping interval */
		    dcl->gui_timer_interval = 500;
		    dcl->idle = 1;
		}
	    }
            break;
#ifdef CONFIG_OPENGL
        case SDL_VIDEORESIZE:
        {
            if (opengl_enabled) {
                SDL_ResizeEvent *rev = &ev->resize;
                real_screen = SDL_SetVideoMode(rev->w, rev->h, 0, SDL_OPENGL|SDL_RESIZABLE);
                opengl_setdata(ds);
                opengl_update(ds, 0, 0, ds_get_width(ds), ds_get_height(ds));
            }
            break;
        }
#endif
        default:
            break;
        }
    }
}

static void sdl_cleanup(void) 
{
#ifdef CONFIG_OPENGL
    if (texture_ref) glDeleteTextures(1, &texture_ref);
#endif
    SDL_Quit();
}

void sdl_display_init(DisplayState *ds, int full_screen, int no_frame, int opengl)
{
    int flags;
    uint8_t data = 0;
    opengl_enabled = opengl;

#if defined(__APPLE__)
    /* always use generic keymaps */
    if (!keyboard_layout)
        keyboard_layout = "en-us";
#endif
    if(keyboard_layout) {
        kbd_layout = init_keyboard_layout(keyboard_layout);
        if (!kbd_layout)
            exit(1);
    }

    if (no_frame)
        gui_noframe = 1;

    flags = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE;
    if (SDL_Init (flags)) {
        fprintf(stderr, "Could not initialize SDL - exiting\n");
        exit(1);
    }
#ifndef _WIN32
    /* NOTE: we still want Ctrl-C to work, so we undo the SDL redirections */
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
#endif

    dcl = qemu_mallocz(sizeof(DisplayChangeListener));
    dcl->dpy_update = sdl_update;
    dcl->dpy_resize = sdl_resize;
    dcl->dpy_refresh = sdl_refresh;
    dcl->dpy_setdata = sdl_setdata;
#ifdef CONFIG_OPENGL
    if (opengl_enabled) {
        dcl->dpy_update = opengl_update;
        dcl->dpy_setdata = opengl_setdata;
    }
#endif
    register_displaychangelistener(ds, dcl);

    if (!opengl_enabled) {
        DisplayAllocator *da;
        const SDL_VideoInfo *vi;

        vi = SDL_GetVideoInfo();
        hostbpp = vi->vfmt->BitsPerPixel;

        da = qemu_mallocz(sizeof(DisplayAllocator));
        da->create_displaysurface = sdl_create_displaysurface;
        da->resize_displaysurface = sdl_resize_displaysurface;
        da->free_displaysurface = sdl_free_displaysurface;
        if (register_displayallocator(ds, da) == da) {
            DisplaySurface *surf;
            surf = sdl_create_displaysurface(ds_get_width(ds), ds_get_height(ds));
            defaultallocator_free_displaysurface(ds->surface);
            ds->surface = surf;
            dpy_resize(ds);
        }
    }

    sdl_update_caption();
    SDL_EnableKeyRepeat(250, 50);
    gui_grab = 0;

    sdl_cursor_hidden = SDL_CreateCursor(&data, &data, 8, 1, 0, 0);
    sdl_cursor_normal = SDL_GetCursor();

    atexit(sdl_cleanup);
    if (full_screen) {
        gui_fullscreen = 1;
        gui_fullscreen_initial_grab = 1;
        sdl_grab_start();
    }
}
