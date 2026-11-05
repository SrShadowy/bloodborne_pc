// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the guest's touch panel, read from a raw evdev node. A DualShock/DualSense touchpad that
// SDL does not expose still shows up in /dev/input, so this file reads it directly and moves its
// two fingers into the panel state bbport_pad_hook_touch.h keeps. Linux only.
#pragma once

#ifdef __linux__

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <SDL3/SDL.h>

// The kernel's input.h may name its own mouse buttons over the guest's names.
#ifndef LINUX_BTN_LEFT
#define LINUX_BTN_LEFT 0x110
#endif
#undef BTN_LEFT
#undef BTN_RIGHT
#undef BTN_BACK
#undef BTN_FORWARD
#undef BTN_SIDE
#undef BTN_EXTRA
#undef BTN_MIDDLE

/// The raw evdev node of a PlayStation touchpad, when SDL does not expose one.
typedef struct ExtraEvdevTouchpad {
    int fd;
    int button_down;
    int finger_down[2];
    uint16_t x[2];
    uint16_t y[2];
    uint8_t id[2];
    int current_slot;
    uint64_t last_scan;
} ExtraEvdevTouchpad;

static ExtraEvdevTouchpad g_extra_evdev_tp = {-1, 0, {0}, {0}, {0}, {1, 2}, 0, 0};

static inline void extra_pad_close_evdev_touchpad(void) {
    if (g_extra_evdev_tp.fd >= 0) {
        close(g_extra_evdev_tp.fd);
        g_extra_evdev_tp.fd = -1;
    }
    g_extra_evdev_tp.button_down = 0;
    g_extra_evdev_tp.finger_down[0] = g_extra_evdev_tp.finger_down[1] = 0;
}

/// Scans /dev/input for a Sony touchpad; -1 when there is none.
static inline int extra_pad_find_evdev_touchpad(void) {
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *ent;
    int found_fd = -1;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        char path[300];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        char name[256] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0) {
            if ((SDL_strcasestr(name, "Touchpad") || SDL_strcasestr(name, "touchpad")) &&
                (SDL_strcasestr(name, "Sony") || SDL_strcasestr(name, "Wireless Controller") ||
                 SDL_strcasestr(name, "DualShock") || SDL_strcasestr(name, "DualSense"))) {
                found_fd = fd;
                break;
            }
        }
        close(fd);
    }
    closedir(d);
    return found_fd;
}

/// Reads the raw node into the same two-touch state SDL's API feeds.
static inline void extra_pad_read_evdev_events(void) {
    struct input_event ev[32];
    ssize_t n;
    while ((n = read(g_extra_evdev_tp.fd, ev, sizeof(ev))) > 0) {
        const int count = (int)(n / sizeof(struct input_event));
        for (int i = 0; i < count; ++i) {
            if (ev[i].type == EV_KEY) {
                if (ev[i].code == LINUX_BTN_LEFT) {
                    g_extra_evdev_tp.button_down = (ev[i].value != 0);
                }
            } else if (ev[i].type == EV_ABS) {
                if (ev[i].code == ABS_MT_SLOT) {
                    if (ev[i].value >= 0 && ev[i].value < 2) {
                        g_extra_evdev_tp.current_slot = ev[i].value;
                    }
                } else if (ev[i].code == ABS_MT_TRACKING_ID) {
                    int slot = g_extra_evdev_tp.current_slot;
                    if (slot >= 0 && slot < 2) {
                        g_extra_evdev_tp.finger_down[slot] = (ev[i].value >= 0);
                        if (ev[i].value >= 0) {
                            g_extra_evdev_tp.id[slot] = (uint8_t)((ev[i].value % BB_TOUCH_ID_MAX) + 1);
                        }
                    }
                } else if (ev[i].code == ABS_MT_POSITION_X ||
                           (g_extra_evdev_tp.current_slot == 0 && ev[i].code == ABS_X)) {
                    int slot = g_extra_evdev_tp.current_slot;
                    if (slot >= 0 && slot < 2) {
                        g_extra_evdev_tp.x[slot] = (uint16_t)(ev[i].value <= 0 ? 0 : ev[i].value >= BB_TOUCHPAD_WIDTH - 1 ? BB_TOUCHPAD_WIDTH - 1 : ev[i].value);
                    }
                } else if (ev[i].code == ABS_MT_POSITION_Y ||
                           (g_extra_evdev_tp.current_slot == 0 && ev[i].code == ABS_Y)) {
                    int slot = g_extra_evdev_tp.current_slot;
                    if (slot >= 0 && slot < 2) {
                        g_extra_evdev_tp.y[slot] = (uint16_t)(ev[i].value <= 0 ? 0 : ev[i].value >= BB_TOUCHPAD_HEIGHT - 1 ? BB_TOUCHPAD_HEIGHT - 1 : ev[i].value);
                    }
                }
            }
        }
    }
    if (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
        extra_pad_close_evdev_touchpad();
    }
}

/// Opens the node (rescanning at most once a second) and moves its state into the panel's.
static inline void extra_pad_poll_evdev_touchpad(void *touches_ptr, uint8_t *touch_count,
                                                 uint32_t *buttons, int *req_left, int *req_right,
                                                 uint64_t now) {
    if (g_extra_evdev_tp.fd < 0) {
        if (g_extra_evdev_tp.last_scan && now < g_extra_evdev_tp.last_scan + 1000000) return;
        g_extra_evdev_tp.last_scan = now;
        g_extra_evdev_tp.fd = extra_pad_find_evdev_touchpad();
        if (g_extra_evdev_tp.fd < 0) return;
    }

    extra_pad_read_evdev_events();
    if (g_extra_evdev_tp.fd < 0) return;

    ExtraPadTouch *touches = (ExtraPadTouch *)touches_ptr;
    for (int s = 0; s < 2; ++s) {
        if (g_extra_evdev_tp.finger_down[s] && *touch_count < 2) {
            int idx = (*touch_count)++;
            touches[idx].x = g_extra_evdev_tp.x[s];
            touches[idx].y = g_extra_evdev_tp.y[s];
            touches[idx].id = g_extra_evdev_tp.id[s];
        }
    }

    if (!g_extra_evdev_tp.button_down) return;
    *buttons |= BB_BUTTON_TOUCHPAD;
    extra_pad_click_side(touches, *touch_count, g_extra_evdev_tp.x[0], req_left, req_right);
}

#endif // __linux__
