# SPDX-License-Identifier: GPL-2.0-or-later
"""Compatibility shims for older versions of Libadwaita and PyGObject (e.g. Ubuntu 22.04 LTS)."""

import gi


def apply_shims():
    """Injects missing widgets and compatibility fixes for older distros and ABI mismatches."""
    # 1. Safely handle GI interface introspection mismatch
    # When older PyGObject (3.42) runs against newer host GTK 4.14+ typelibs,
    # new uninitialized interfaces (e.g. AccessibleText) can raise TypeError: must be an interface.
    try:
        import gi.module
        if hasattr(gi.module, "get_interfaces_for_object"):
            def safe_get_interfaces_for_object(object_info):
                interfaces = []
                for interface_info in object_info.get_interfaces():
                    try:
                        namespace = interface_info.get_namespace()
                        name = interface_info.get_name()
                        import importlib
                        module = importlib.import_module("gi.repository." + namespace)
                        cls = getattr(module, name)
                        from gi.repository import GObject
                        if isinstance(cls, type) and issubclass(cls, GObject.GInterface):
                            interfaces.append(cls)
                    except Exception:
                        pass
                return interfaces

            gi.module.get_interfaces_for_object = safe_get_interfaces_for_object
    except Exception:
        pass

    # 2. Safely handle enum values introduced in newer GTK typelibs (e.g. GTK 4.12+ Gtk.Align: 5)
    # PyGObject 3.42 throws ValueError: invalid enum value when typelib contains new enum entries.
    try:
        import gi.module
        orig_getattr = gi.module.IntrospectionModule.__getattr__

        def safe_getattr(self, name):
            try:
                return orig_getattr(self, name)
            except ValueError as e:
                if "invalid enum value" in str(e):
                    info = gi.module.repository.find_by_name(self._namespace, name)
                    if isinstance(info, gi.module.EnumInfo):
                        g_type = info.get_g_type()
                        wrapper = g_type.pytype
                        if wrapper is None:
                            if info.is_flags():
                                wrapper = gi.module.flags_add(g_type) if g_type.is_a(gi.module.TYPE_FLAGS) else gi.module.flags_register_new_gtype_and_add(info)
                            else:
                                wrapper = gi.module.enum_add(g_type) if g_type.is_a(gi.module.TYPE_ENUM) else gi.module.enum_register_new_gtype_and_add(info)
                            wrapper.__info__ = info
                            wrapper.__module__ = "gi.repository." + info.get_namespace()
                            trans = "".maketrans("abcdefgjhijklmnopqrstuvwxyz", "ABCDEFGJHIJKLMNOPQRSTUVWXYZ")
                            for value_info in info.get_values():
                                v_name = value_info.get_name_unescaped().translate(trans)
                                try:
                                    val = wrapper(value_info.get_value())
                                except ValueError:
                                    val = value_info.get_value()
                                setattr(wrapper, v_name, val)
                            for method_info in info.get_methods():
                                setattr(wrapper, method_info.__name__, method_info)
                            if g_type != gi.module.TYPE_NONE:
                                g_type.pytype = wrapper
                        self.__dict__[name] = wrapper
                        return wrapper
                raise

        gi.module.IntrospectionModule.__getattr__ = safe_getattr
    except Exception:
        pass

    gi.require_version("Gtk", "4.0")
    gi.require_version("Adw", "1")
    from gi.repository import Gtk, Adw

    # 2. ViewStack add_titled_with_icon (added in Libadwaita 1.4)
    if hasattr(Adw, "ViewStack") and not hasattr(Adw.ViewStack, "add_titled_with_icon"):
        def add_titled_with_icon(self, child, name, title, icon):
            page = self.add_titled(child, name, title)
            if hasattr(page, "set_icon_name"):
                page.set_icon_name(icon)
            return page

        Adw.ViewStack.add_titled_with_icon = add_titled_with_icon

    # 3. ToolbarView (added in Libadwaita 1.4)
    if not hasattr(Adw, "ToolbarView"):
        class ToolbarView(Gtk.Box):
            def __init__(self, **kwargs):
                super().__init__(orientation=Gtk.Orientation.VERTICAL, **kwargs)
                self.top_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
                self.content_holder = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, vexpand=True, hexpand=True)
                self.bottom_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
                super().append(self.top_box)
                super().append(self.content_holder)
                super().append(self.bottom_box)

            def add_top_bar(self, widget):
                self.top_box.append(widget)

            def set_content(self, widget):
                while child := self.content_holder.get_first_child():
                    self.content_holder.remove(child)
                widget.set_vexpand(True)
                widget.set_hexpand(True)
                self.content_holder.append(widget)

            def add_bottom_bar(self, widget):
                self.bottom_box.append(widget)

        Adw.ToolbarView = ToolbarView

    # 4. SwitchRow (added in Libadwaita 1.4)
    if not hasattr(Adw, "SwitchRow"):
        class SwitchRow(Adw.ActionRow):
            def __init__(self, **kwargs):
                active = kwargs.pop("active", False)
                super().__init__(**kwargs)
                self.switch = Gtk.Switch(active=active, valign=Gtk.Align.CENTER)
                self.add_suffix(self.switch)
                self.set_activatable_widget(self.switch)

            def get_active(self):
                return self.switch.get_active()

            def set_active(self, val):
                self.switch.set_active(val)

            def connect(self, signal, callback, *args):
                if signal in ("notify::active", "state-set"):
                    return self.switch.connect(signal, callback, *args)
                return super().connect(signal, callback, *args)

        Adw.SwitchRow = SwitchRow

    # 5. SpinRow (added in Libadwaita 1.2 / 1.4)
    if not hasattr(Adw, "SpinRow"):
        class SpinRow(Adw.ActionRow):
            @classmethod
            def new_with_range(cls, min_val, max_val, step):
                row = cls()
                row.spin = Gtk.SpinButton.new_with_range(min_val, max_val, step)
                row.spin.set_valign(Gtk.Align.CENTER)
                row.add_suffix(row.spin)
                return row

            def get_value(self):
                return self.spin.get_value()

            def set_value(self, val):
                self.spin.set_value(val)

            def set_digits(self, digits):
                self.spin.set_digits(digits)

            def get_digits(self):
                return self.spin.get_digits()

            def set_range(self, min_val, max_val):
                self.spin.set_range(min_val, max_val)

            def set_increments(self, step, page):
                self.spin.set_increments(step, page)

            def connect(self, signal, callback, *args):
                if signal in ("notify::value", "value-changed"):
                    return self.spin.connect(signal, callback, *args)
                return super().connect(signal, callback, *args)

            def __getattr__(self, name):
                if hasattr(self, "spin") and hasattr(self.spin, name):
                    return getattr(self.spin, name)
                raise AttributeError(f"'{type(self).__name__}' object has no attribute '{name}'")

        Adw.SpinRow = SpinRow

    # 6. EntryRow (added in Libadwaita 1.2)
    if not hasattr(Adw, "EntryRow"):
        class EntryRow(Adw.ActionRow):
            def __init__(self, **kwargs):
                text = kwargs.pop("text", "")
                super().__init__(**kwargs)
                self.entry = Gtk.Entry(valign=Gtk.Align.CENTER, hexpand=True)
                if text:
                    self.entry.set_text(text)
                self.add_suffix(self.entry)
                self.set_activatable_widget(self.entry)

            def get_text(self):
                return self.entry.get_text()

            def set_text(self, text):
                self.entry.set_text(text)

            def connect(self, signal, callback, *args):
                if signal in ("changed", "notify::text", "activate", "entry-activated"):
                    return self.entry.connect(signal, callback, *args)
                return super().connect(signal, callback, *args)

        Adw.EntryRow = EntryRow

    # 7. PasswordEntryRow (added in Libadwaita 1.2)
    if not hasattr(Adw, "PasswordEntryRow"):
        class PasswordEntryRow(Adw.ActionRow):
            def __init__(self, **kwargs):
                text = kwargs.pop("text", "")
                super().__init__(**kwargs)
                self.entry = Gtk.PasswordEntry(valign=Gtk.Align.CENTER, hexpand=True)
                if text:
                    self.entry.set_text(text)
                self.add_suffix(self.entry)
                self.set_activatable_widget(self.entry)

            def get_text(self):
                return self.entry.get_text()

            def set_text(self, text):
                self.entry.set_text(text)

            def connect(self, signal, callback, *args):
                if signal in ("changed", "notify::text", "activate", "entry-activated"):
                    return self.entry.connect(signal, callback, *args)
                return super().connect(signal, callback, *args)

        Adw.PasswordEntryRow = PasswordEntryRow
