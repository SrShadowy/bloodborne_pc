# SPDX-License-Identifier: GPL-2.0-or-later
"""Full gamepad navigation support for BBPort launcher using SDL3.

Enables seamless controller navigation (Xbox, PlayStation DualShock 4/DualSense,
Nintendo Switch Pro, Steam Deck, and generic gamepads) through the launcher menus:
- D-Pad & Left Stick: Navigate settings up/down, adjust sliders/combos left/right
- Right Stick: Smoothly scroll active page
- Button A (South / Cross): Activate / Toggle switch / Open dropdown / Click button
- Button B (East / Circle): Back / Collapse expander / Close dialog
- Button Y (North / Triangle): Focus main Launch button
- Button Start (Menu / Options): Immediately launch the game
- Left / Right Shoulder (LB / RB, L1 / R1): Cycle launcher tabs
"""

import ctypes
import ctypes.util
import time

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, GLib, Gtk

from bbport_i18n import tr


# --- SDL3 CTypes Interface ---------------------------------------------------------------

def _load_sdl3():
    candidates = [
        "libSDL3.so.0",
        "libSDL3.so",
        ctypes.util.find_library("SDL3"),
    ]
    for name in candidates:
        if not name:
            continue
        try:
            lib = ctypes.CDLL(name)
            if hasattr(lib, "SDL_Init"):
                return lib
        except OSError:
            pass
    return None


_SDL3 = _load_sdl3()

if _SDL3:
    _SDL3.SDL_Init.argtypes = [ctypes.c_uint32]
    _SDL3.SDL_Init.restype = ctypes.c_bool

    _SDL3.SDL_GetGamepads.argtypes = [ctypes.POINTER(ctypes.c_int)]
    _SDL3.SDL_GetGamepads.restype = ctypes.POINTER(ctypes.c_uint32)

    _SDL3.SDL_OpenGamepad.argtypes = [ctypes.c_uint32]
    _SDL3.SDL_OpenGamepad.restype = ctypes.c_void_p

    _SDL3.SDL_GamepadConnected.argtypes = [ctypes.c_void_p]
    _SDL3.SDL_GamepadConnected.restype = ctypes.c_bool

    _SDL3.SDL_GetGamepadName.argtypes = [ctypes.c_void_p]
    _SDL3.SDL_GetGamepadName.restype = ctypes.c_char_p

    _SDL3.SDL_GetGamepadButton.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _SDL3.SDL_GetGamepadButton.restype = ctypes.c_bool

    _SDL3.SDL_GetGamepadAxis.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _SDL3.SDL_GetGamepadAxis.restype = ctypes.c_int16

    _SDL3.SDL_CloseGamepad.argtypes = [ctypes.c_void_p]
    _SDL3.SDL_CloseGamepad.restype = None

    _SDL3.SDL_PumpEvents.argtypes = []
    _SDL3.SDL_PumpEvents.restype = None


# SDL_GamepadButton enum values
BTN_SOUTH = 0           # A (Xbox) / Cross (PlayStation)
BTN_EAST = 1            # B (Xbox) / Circle (PlayStation)
BTN_WEST = 2            # X (Xbox) / Square (PlayStation)
BTN_NORTH = 3           # Y (Xbox) / Triangle (PlayStation)
BTN_BACK = 4            # Back / View / Share
BTN_GUIDE = 5           # Xbox / PS button
BTN_START = 6           # Start / Menu / Options
BTN_LEFT_STICK = 7      # L3
BTN_RIGHT_STICK = 8     # R3
BTN_LEFT_SHOULDER = 9   # LB / L1
BTN_RIGHT_SHOULDER = 10 # RB / R1
BTN_DPAD_UP = 11
BTN_DPAD_DOWN = 12
BTN_DPAD_LEFT = 13
BTN_DPAD_RIGHT = 14

# SDL_GamepadAxis enum values
AXIS_LEFTX = 0
AXIS_LEFTY = 1
AXIS_RIGHTX = 2
AXIS_RIGHTY = 3

STICK_DEADZONE = 16000
REPEAT_DELAY = 0.32
REPEAT_RATE = 0.10


# --- UI Helpers --------------------------------------------------------------------------

def _is_descendant(widget, ancestor):
    p = widget
    while p:
        if p == ancestor:
            return True
        p = p.get_parent()
    return False


def _get_interactive_targets(root):
    """Gathers all focusable interactive widgets within root in visual hierarchy order."""
    targets = []

    def walk(w):
        if not w.get_visible() or not w.get_sensitive():
            return
        if isinstance(w, Adw.ExpanderRow):
            targets.append(w)
            if w.get_expanded():
                child = w.get_first_child()
                while child:
                    if isinstance(child, Gtk.Revealer):
                        walk(child)
                    else:
                        c2 = child.get_first_child()
                        while c2:
                            if isinstance(c2, Gtk.Revealer):
                                walk(c2)
                            c2 = c2.get_next_sibling()
                    child = child.get_next_sibling()
            return
        elif isinstance(w, Adw.PreferencesRow):
            targets.append(w)
            return
        elif isinstance(w, (Gtk.Button, Gtk.CheckButton)) and not w.has_css_class("tab-bar-button"):
            targets.append(w)
            return
        child = w.get_first_child()
        while child:
            walk(child)
            child = child.get_next_sibling()

    walk(root)
    return targets


def _find_active_target(window, targets):
    focused = window.get_focus()
    if not focused:
        return -1
    for idx, t in enumerate(targets):
        if t == focused or _is_descendant(focused, t):
            return idx
    return -1


# --- Gamepad Navigator -------------------------------------------------------------------

class GamepadNavigator:
    """Monitors controller inputs via SDL3 and maps them to GTK4/Libadwaita navigation."""

    TAB_NAMES = ["overview", "graphics", "performance", "controls", "online", "mods", "log"]

    def __init__(self, window):
        self.window = window
        self.pad_handle = None
        self.pad_name = ""
        self.prev_buttons = [False] * 21
        self.prev_up = False
        self.prev_down = False
        self.prev_left = False
        self.prev_right = False
        self.repeat_dir = None
        self.repeat_start_time = 0.0
        self.repeat_last_time = 0.0
        self.poll_timer_id = None
        self.is_active = False

        if not _SDL3:
            return

        try:
            _SDL3.SDL_Init(0x00002000)  # SDL_INIT_GAMEPAD
        except Exception:
            return

        self.poll_timer_id = GLib.timeout_add(16, self._on_poll)

    def close(self):
        if self.poll_timer_id:
            GLib.source_remove(self.poll_timer_id)
            self.poll_timer_id = None
        if self.pad_handle and _SDL3:
            try:
                _SDL3.SDL_CloseGamepad(self.pad_handle)
            except Exception:
                pass
            self.pad_handle = None

    def _ensure_gamepad(self):
        if self.pad_handle:
            if _SDL3.SDL_GamepadConnected(self.pad_handle):
                return True
            try:
                _SDL3.SDL_CloseGamepad(self.pad_handle)
            except Exception:
                pass
            self.pad_handle = None
            self.pad_name = ""
            if hasattr(self.window, "set_gamepad_hints_visible"):
                self.window.set_gamepad_hints_visible(False)

        count = ctypes.c_int(0)
        gamepads = _SDL3.SDL_GetGamepads(ctypes.byref(count))
        if count.value > 0 and gamepads:
            self.pad_handle = _SDL3.SDL_OpenGamepad(gamepads[0])
            if self.pad_handle:
                name_bytes = _SDL3.SDL_GetGamepadName(self.pad_handle)
                self.pad_name = name_bytes.decode("utf-8", "replace") if name_bytes else "Gamepad"
                if hasattr(self.window, "set_gamepad_hints_visible"):
                    self.window.set_gamepad_hints_visible(True, self.pad_name)
                return True
        return False

    def _reset_input_state(self):
        self.prev_buttons = [False] * 21
        self.prev_up = False
        self.prev_down = False
        self.prev_left = False
        self.prev_right = False
        self.repeat_dir = None

    def _on_poll(self):
        if not self.window or not self.window.get_visible():
            return True

        # When the game is running, the controller belongs strictly to the game process.
        # Suspend launcher controller navigation so in-game button presses (e.g. Start)
        # do not trigger launcher actions or stop the game.
        if getattr(self.window, "process", None) is not None:
            self._reset_input_state()
            return True

        # When the launcher window is unfocused / in the background, ignore controller inputs.
        if hasattr(self.window, "is_active") and not self.window.is_active():
            self._reset_input_state()
            return True

        if getattr(self.window, "input_assignment_active", False):
            return True

        if not _SDL3:
            return False

        try:
            _SDL3.SDL_PumpEvents()
        except Exception:
            return True

        if not self._ensure_gamepad():
            return True

        pad = self.pad_handle
        now = time.monotonic()

        # Read buttons
        btns = [_SDL3.SDL_GetGamepadButton(pad, i) for i in range(15)]
        lx = _SDL3.SDL_GetGamepadAxis(pad, AXIS_LEFTX)
        ly = _SDL3.SDL_GetGamepadAxis(pad, AXIS_LEFTY)
        ry = _SDL3.SDL_GetGamepadAxis(pad, AXIS_RIGHTY)

        # Directional states (D-pad + Left stick)
        up = btns[BTN_DPAD_UP] or (ly < -STICK_DEADZONE)
        down = btns[BTN_DPAD_DOWN] or (ly > STICK_DEADZONE)
        left = btns[BTN_DPAD_LEFT] or (lx < -STICK_DEADZONE)
        right = btns[BTN_DPAD_RIGHT] or (lx > STICK_DEADZONE)

        # 1. Check if a ComboRow dropdown / choice popover is open
        pop = self._get_active_popover()
        if pop:
            return self._handle_popover_input(pop, btns, up, down, left, right, ry, now)

        # 2. Check if a modal / alert choice dialog is open
        dialog = self._get_active_dialog()
        if dialog:
            return self._handle_dialog_input(dialog, btns, up, down, left, right, now)

        # Right stick smooth page scrolling
        if abs(ry) > STICK_DEADZONE:
            self._scroll_active_page(ry)

        # Directional repetition logic
        current_dir = "up" if up else ("down" if down else ("left" if left else ("right" if right else None)))
        dir_triggered = False

        if current_dir:
            if self.repeat_dir != current_dir:
                self.repeat_dir = current_dir
                self.repeat_start_time = now
                self.repeat_last_time = now
                dir_triggered = True
            elif (now - self.repeat_start_time >= REPEAT_DELAY) and (now - self.repeat_last_time >= REPEAT_RATE):
                self.repeat_last_time = now
                dir_triggered = True
        else:
            self.repeat_dir = None

        if dir_triggered:
            if self.repeat_dir == "up":
                self._navigate(-1)
            elif self.repeat_dir == "down":
                self._navigate(1)
            elif self.repeat_dir == "left":
                self._adjust(-1)
            elif self.repeat_dir == "right":
                self._adjust(1)

        # Edge-triggered button events (pressed on this frame)
        def pressed(idx):
            return btns[idx] and not self.prev_buttons[idx]

        if pressed(BTN_LEFT_SHOULDER):
            self._cycle_tabs(-1)
        elif pressed(BTN_RIGHT_SHOULDER):
            self._cycle_tabs(1)
        elif pressed(BTN_SOUTH):
            self._activate_focused()
        elif pressed(BTN_EAST):
            self._go_back()
        elif pressed(BTN_NORTH):
            self.window.launch_button.grab_focus()
        elif pressed(BTN_START):
            if getattr(self.window, "process", None) is None and self.window.launch_button.get_sensitive():
                self.window.launch_button.emit("clicked")

        self.prev_buttons = btns
        self.prev_up = up
        self.prev_down = down
        self.prev_left = left
        self.prev_right = right

        return True

    # --- Popover (ComboRow Dropdown) Handling ------------------------------------------------

    def _get_active_popover(self):
        def find_pop(w):
            c = w.get_first_child()
            while c:
                if isinstance(c, Gtk.Popover) and c.get_visible():
                    return c
                p = find_pop(c)
                if p:
                    return p
                c = c.get_next_sibling()
            return None
        return find_pop(self.window)

    def _get_popover_items(self, pop):
        items = []
        def walk(w):
            if not w.get_visible() or not w.get_sensitive():
                return
            if type(w).__name__ == "GtkListItemWidget":
                items.append(w)
                return
            elif isinstance(w, (Gtk.Button, Gtk.CheckButton)) and not w.has_css_class("tab-bar-button"):
                items.append(w)
                return
            c = w.get_first_child()
            while c:
                walk(c)
                c = c.get_next_sibling()
        walk(pop)
        return items

    def _get_popover_combo_row(self, pop):
        p = pop.get_parent()
        while p:
            if isinstance(p, Adw.ComboRow):
                return p
            p = p.get_parent()
        return None

    def _get_popover_listview(self, pop):
        def find_lv(w):
            if isinstance(w, Gtk.ListView):
                return w
            c = w.get_first_child()
            while c:
                res = find_lv(c)
                if res:
                    return res
                c = c.get_next_sibling()
            return None
        return find_lv(pop)

    def _get_popover_scroller(self, pop):
        def find_scroller(w):
            if isinstance(w, Gtk.ScrolledWindow):
                return w
            c = w.get_first_child()
            while c:
                res = find_scroller(c)
                if res:
                    return res
                c = c.get_next_sibling()
            return None
        return find_scroller(pop)

    def _handle_popover_input(self, pop, btns, up, down, left, right, ry, now):
        items = self._get_popover_items(pop)
        row = self._get_popover_combo_row(pop)
        lv = self._get_popover_listview(pop)

        # Right stick smooth scrolling inside popover
        if abs(ry) > STICK_DEADZONE:
            scroller = self._get_popover_scroller(pop)
            if scroller:
                adj = scroller.get_vadjustment()
                if adj:
                    delta_val = (ry / 32768.0) * 16.0
                    adj.set_value(max(adj.get_lower(), min(adj.get_upper() - adj.get_page_size(), adj.get_value() + delta_val)))

        focused = self.window.get_focus()
        curr_idx = -1
        if items:
            for i, item in enumerate(items):
                if item == focused or _is_descendant(focused, item):
                    curr_idx = i
                    break
            if curr_idx == -1:
                sel = row.get_selected() if row else 0
                curr_idx = max(0, min(len(items) - 1, sel))
                if lv:
                    try:
                        lv.scroll_to(curr_idx, Gtk.ListScrollFlags.FOCUS | Gtk.ListScrollFlags.SELECT, None)
                    except Exception:
                        pass
                items[curr_idx].grab_focus()

        # Directional navigation repetition (Up / Down)
        current_dir = "up" if up else ("down" if down else None)
        dir_triggered = False

        if current_dir:
            if self.repeat_dir != current_dir:
                self.repeat_dir = current_dir
                self.repeat_start_time = now
                self.repeat_last_time = now
                dir_triggered = True
            elif (now - self.repeat_start_time >= REPEAT_DELAY) and (now - self.repeat_last_time >= REPEAT_RATE):
                self.repeat_last_time = now
                dir_triggered = True
        else:
            self.repeat_dir = None

        if dir_triggered and items:
            delta = -1 if self.repeat_dir == "up" else 1
            new_idx = max(0, min(len(items) - 1, curr_idx + delta))
            if lv:
                try:
                    lv.scroll_to(new_idx, Gtk.ListScrollFlags.FOCUS | Gtk.ListScrollFlags.SELECT, None)
                except Exception:
                    pass
            items[new_idx].grab_focus()
            curr_idx = new_idx

        def pressed(idx):
            return btns[idx] and not self.prev_buttons[idx]

        if pressed(BTN_SOUTH):  # A: Confirm choice
            if row and curr_idx != -1 and curr_idx < len(items):
                row.set_selected(curr_idx)
            pop.popdown()
            if row:
                row.grab_focus()
        elif pressed(BTN_EAST):  # B: Cancel / Close popover
            pop.popdown()
            if row:
                row.grab_focus()

        self.prev_buttons = btns
        self.prev_up = up
        self.prev_down = down
        self.prev_left = left
        self.prev_right = right
        return True

    # --- Modal Dialog Handling -------------------------------------------------------------

    def _get_active_dialog(self):
        app = self.window.get_application()
        if app:
            windows = app.get_windows()
            if len(windows) > 1:
                top = windows[-1]
                if top != self.window and top.get_visible():
                    return top

        def find_adw_dialog(w):
            c = w.get_first_child()
            while c:
                if isinstance(c, (Adw.Dialog, Adw.AlertDialog)) and c.get_visible():
                    return c
                res = find_adw_dialog(c)
                if res:
                    return res
                c = c.get_next_sibling()
            return None
        return find_adw_dialog(self.window)

    def _get_dialog_targets(self, dialog):
        targets = []
        def walk(w):
            if not w.get_visible() or not w.get_sensitive():
                return
            if isinstance(w, Gtk.Button):
                targets.append(w)
                return
            c = w.get_first_child()
            while c:
                walk(c)
                c = c.get_next_sibling()
        walk(dialog)
        return targets

    def _handle_dialog_input(self, dialog, btns, up, down, left, right, now):
        targets = self._get_dialog_targets(dialog)
        focused = self.window.get_focus()

        curr_idx = -1
        if targets:
            for i, t in enumerate(targets):
                if t == focused or _is_descendant(focused, t):
                    curr_idx = i
                    break
            if curr_idx == -1:
                curr_idx = 0
                targets[0].grab_focus()

        current_dir = "prev" if (left or up) else ("next" if (right or down) else None)
        dir_triggered = False

        if current_dir:
            if self.repeat_dir != current_dir:
                self.repeat_dir = current_dir
                self.repeat_start_time = now
                self.repeat_last_time = now
                dir_triggered = True
            elif (now - self.repeat_start_time >= REPEAT_DELAY) and (now - self.repeat_last_time >= REPEAT_RATE):
                self.repeat_last_time = now
                dir_triggered = True
        else:
            self.repeat_dir = None

        if dir_triggered and targets:
            delta = -1 if self.repeat_dir == "prev" else 1
            new_idx = max(0, min(len(targets) - 1, curr_idx + delta))
            targets[new_idx].grab_focus()
            curr_idx = new_idx

        def pressed(idx):
            return btns[idx] and not self.prev_buttons[idx]

        if pressed(BTN_SOUTH):  # A: Activate button
            if curr_idx != -1 and curr_idx < len(targets):
                targets[curr_idx].emit("clicked")
        elif pressed(BTN_EAST):  # B: Cancel / Close dialog
            cancel_btn = None
            for t in targets:
                lbl = getattr(t, "get_label", lambda: "")() or ""
                if any(w in lbl.lower() for w in ["cancel", "отмена", "cancelar"]):
                    cancel_btn = t
                    break
            if cancel_btn:
                cancel_btn.emit("clicked")
            elif hasattr(dialog, "force_close"):
                dialog.force_close()
            elif hasattr(dialog, "close"):
                dialog.close()

        self.prev_buttons = btns
        self.prev_up = up
        self.prev_down = down
        self.prev_left = left
        self.prev_right = right
        return True

    def _scroll_active_page(self, stick_val):
        page = self.window.stack.get_visible_child()
        if not page:
            return

        def find_scroller(w):
            if isinstance(w, Gtk.ScrolledWindow):
                return w
            c = w.get_first_child()
            while c:
                res = find_scroller(c)
                if res:
                    return res
                c = c.get_next_sibling()
            return None

        scroller = find_scroller(page)
        if scroller:
            adj = scroller.get_vadjustment()
            if adj:
                delta = (stick_val / 32768.0) * 16.0
                adj.set_value(max(adj.get_lower(), min(adj.get_upper() - adj.get_page_size(), adj.get_value() + delta)))

    def _cycle_tabs(self, delta):
        cur_name = self.window.stack.get_visible_child_name()
        if cur_name in self.TAB_NAMES:
            idx = self.TAB_NAMES.index(cur_name)
            new_idx = (idx + delta) % len(self.TAB_NAMES)
            new_name = self.TAB_NAMES[new_idx]
            if new_name in self.window.tab_buttons:
                self.window.tab_buttons[new_name].set_active(True)
                GLib.idle_add(self._focus_first_target)

    def _focus_first_target(self):
        page = self.window.stack.get_visible_child()
        if not page:
            return False
        targets = _get_interactive_targets(page)
        if targets:
            targets[0].grab_focus()
        return False

    def _navigate(self, delta):
        # If launch_button has focus and down is pressed, move to page targets
        if self.window.launch_button == self.window.get_focus():
            if delta > 0:
                self._focus_first_target()
            return

        page = self.window.stack.get_visible_child()
        if not page:
            return
        targets = _get_interactive_targets(page)
        if not targets:
            return

        idx = _find_active_target(self.window, targets)
        if idx == -1:
            new_idx = 0 if delta > 0 else (len(targets) - 1)
        else:
            new_idx = idx + delta
            if new_idx < 0:
                # Wrap to headerbar launch button when scrolling past top
                self.window.launch_button.grab_focus()
                return
            elif new_idx >= len(targets):
                new_idx = len(targets) - 1

        targets[new_idx].grab_focus()

    def _adjust(self, delta):
        focused = self.window.get_focus()
        if not focused:
            return

        # Walk up to find target row
        target = focused
        while target and not isinstance(target, (Adw.PreferencesRow, Gtk.Button, Gtk.CheckButton)):
            target = target.get_parent()

        if not target:
            return

        if isinstance(target, Adw.SpinRow):
            adj = target.get_adjustment()
            step = adj.get_step_increment()
            val = target.get_value() + (delta * step)
            target.set_value(max(adj.get_lower(), min(adj.get_upper(), val)))
        elif isinstance(target, Adw.ComboRow):
            sel = target.get_selected()
            model = target.get_model()
            if model:
                n = model.get_n_items()
                new_sel = max(0, min(n - 1, sel + delta))
                if new_sel != sel:
                    target.set_selected(new_sel)
        elif isinstance(target, Adw.SwitchRow):
            target.set_active(delta > 0)
        elif isinstance(target, (Adw.ActionRow, Adw.PreferencesRow)):
            btns = []
            def walk_btns(w):
                if isinstance(w, (Gtk.Button, Gtk.CheckButton)):
                    btns.append(w)
                    return
                c = w.get_first_child()
                while c:
                    walk_btns(c)
                    c = c.get_next_sibling()
            walk_btns(target)
            if btns:
                if focused == target:
                    if delta > 0:
                        btns[0].grab_focus()
                elif focused in btns:
                    curr_btn_idx = btns.index(focused)
                    next_btn_idx = curr_btn_idx + delta
                    if 0 <= next_btn_idx < len(btns):
                        btns[next_btn_idx].grab_focus()
                    elif next_btn_idx < 0:
                        target.grab_focus()

    def _activate_focused(self):
        focused = self.window.get_focus()
        if not focused:
            self._focus_first_target()
            return

        target = focused
        while target and not isinstance(target, (Adw.PreferencesRow, Gtk.Button, Gtk.CheckButton)):
            target = target.get_parent()

        if not target:
            return

        if isinstance(target, Gtk.CheckButton):
            target.set_active(not target.get_active())
        elif isinstance(target, Gtk.Button):
            target.emit("clicked")
        elif isinstance(target, Adw.SwitchRow):
            target.set_active(not target.get_active())
        elif isinstance(target, Adw.ExpanderRow):
            target.set_expanded(not target.get_expanded())
        elif isinstance(target, Adw.ComboRow):
            target.activate()
        elif isinstance(target, (Adw.ActionRow, Adw.PreferencesRow)):
            act = getattr(target, "get_activatable_widget", lambda: None)()
            if act and isinstance(act, Gtk.Button):
                act.emit("clicked")
            elif act:
                act.activate()
            else:
                btns = []
                def walk_btns(w):
                    if isinstance(w, (Gtk.Button, Gtk.CheckButton)):
                        btns.append(w)
                        return
                    c = w.get_first_child()
                    while c:
                        walk_btns(c)
                        c = c.get_next_sibling()
                walk_btns(target)
                if btns:
                    btns[0].emit("clicked")
                else:
                    target.activate()

    def _go_back(self):
        focused = self.window.get_focus()
        if focused:
            target = focused
            while target and not isinstance(target, Adw.ExpanderRow):
                target = target.get_parent()
            if isinstance(target, Adw.ExpanderRow) and target.get_expanded():
                target.set_expanded(False)
                target.grab_focus()
                return

        # Check for active modals
        app = self.window.get_application()
        if app:
            windows = app.get_windows()
            if len(windows) > 1:
                windows[-1].close()
                return

        # Return to overview tab if on another page
        if self.window.stack.get_visible_child_name() != "overview":
            if "overview" in self.window.tab_buttons:
                self.window.tab_buttons["overview"].set_active(True)
                GLib.idle_add(self._focus_first_target)
