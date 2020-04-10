#!/usr/bin/python3

import gi
from gi.repository import GObject, Gio, GLib, Gdk

# We shouldn't need this class but appindicator doesn't cache their properties so it's better
# to hide the ugliness of fetching properties in here. If this situation changes it will be easier
# to modify the behavior on its own.

APPINDICATOR_PATH_PREFIX = "/org/ayatana/NotificationItem/"

class SnItem(GObject.Object):
    __gsignals__ = {
        "ready": (GObject.SignalFlags.RUN_LAST, None, ()),
        "update-icon": (GObject.SignalFlags.RUN_LAST, None, ()),
        "update-status": (GObject.SignalFlags.RUN_LAST, None, ()),
        "update-menu": (GObject.SignalFlags.RUN_LAST, None, ()),
        "update-tooltip": (GObject.SignalFlags.RUN_LAST, None, ())
    }
    def __init__(self, bus_name, path):
        GObject.Object.__init__(self)

        self.bus_name = bus_name
        self.path = path

        self.prop_proxy = None
        self.ready = False
        self.update_icon_id = 0
        self.sn_subscription_id = 0

        self._status = "Active"

        self._session = Gio.bus_get(Gio.BusType.SESSION, None, self.bus_get_callback)

    def bus_get_callback(self, source, result, data=None):
        try:
            self._session = Gio.bus_get_finish(result)
        except GLib.Error as e:
            print(e.message)
            # FIXME: what to do here?
            return

        self.sn_subscription_id = self._session.signal_subscribe(None,
                                                                 "org.kde.StatusNotifierItem",
                                                                 None,
                                                                 None, # we can't match paths
                                                                 None, # competing standards
                                                                 Gio.DBusSignalFlags.NONE,
                                                                 self.signal_received,
                                                                 None,
                                                                 None)

        self.emit("ready")

    def signal_received(self, connection, sender, path, iface, signal, params, data=None, wtfisthis=None):
        if sender != self.bus_name:
            return
        print("Signal from %s: %s" % (self.bus_name, signal))
        if signal in ("NewIcon",
                      "NewAttentionIcon",
                      "NewOverlayIcon"):
            pass
            # self._emit_update_icon_signal()
        elif signal == "NewStatus":
            # libappindicator sends NewStatus during its dispose phase - by the time we want to act
            # on it, we can no longer fetch the status via Get, so we'll cache the status we receive
            # in the signal, in case this happens we can send it as a default with our own update-status
            # signal.
            self._status = parameters[0]
            self.emit("update-status")
        elif signal in ("NewMenu"):
            pass
            # self.emit("update-menu")
        elif signal in ("XAyatanaNewLabel",
                        "Tooltip"):
            self.emit("update-tooltip")

    def _emit_update_icon_signal(self):
        if self.update_icon_id > 0:
            GLib.source_remove(self.update_icon_id)
            self.update_icon_id = 0

        self.update_icon_id = GLib.timeout_add(25, self._emit_update_icon_cb)

    def _emit_update_icon_cb(self):
        # if self.sn_item_proxy != None:
        self.emit("update-icon")

        self.update_icon_id = 0
        return GLib.SOURCE_REMOVE

    def _get_property(self, name):
        res = self._session.call_sync(self.bus_name,
                                      self.path,
                                      "org.freedesktop.DBus.Properties",
                                      "Get",
                                      GLib.Variant("(ss)",
                                                   ("org.kde.StatusNotifierItem",
                                                    name)),
                                      GLib.VariantType("(v)"),
                                      Gio.DBusCallFlags.NONE,
                                      5 * 1000,
                                      None)
        return res

    def _call_sn_item_method(self, method_name, in_args):
        self._session.call(self.bus_name,
                           self.path,
                           "org.kde.StatusNotifierItem",
                           method_name,
                           in_args,
                           None,
                           Gio.DBusCallFlags.NONE,
                           5 * 1000,
                           None, # Don't care about a callback at this time, nothing returns
                           None)

    def _call_sn_item_method_sync(self, method_name, in_args):
        return self._session.call_sync(self.bus_name,
                                       self.path,
                                       "org.kde.StatusNotifierItem",
                                       method_name,
                                       in_args,
                                       None,
                                       Gio.DBusCallFlags.NONE,
                                       5 * 1000,
                                       None)

    def _get_string_prop(self, name, default=""):
        try:
            res = self._get_property(name)
            if res[0] == "":
                return default
            return res[0]
        except GLib.Error as e:
            if e.code not in (Gio.DBusError.UNKNOWN_PROPERTY, Gio.DBusError.INVALID_ARGS):
                print("Couldn't get %s property: %s... or this is libappindicator's closing Status update" % (name, e.message))

            return default

    def _get_bool_prop(self, name, default=False):
        try:
            res = self._get_property(name)

            return res[0]
        except GLib.Error as e:
            if e.code not in (Gio.DBusError.UNKNOWN_PROPERTY, Gio.DBusError.INVALID_ARGS):
                print("Couldn't get %s property: %s" % (name, e.message))
            return default

    def _get_pixmap_array_prop(self, name, default=None):
        try:
            res = self._get_property(name)
            if res[0] == "":
                return default

            return res[0]
        except GLib.Error as e:
            if e.code not in (Gio.DBusError.UNKNOWN_PROPERTY, Gio.DBusError.INVALID_ARGS):
                print("Couldn't get %s property: %s" % (name, e.message))
            return default

    def category            (self): return self._get_string_prop("Category", "ApplicationStatus")
    def id                  (self): return self._get_string_prop("Id")
    def title               (self): return self._get_string_prop("Title")
    def status               (self): return self._get_string_prop("Status", self._status)
    def menu                (self): return self._get_string_prop("Menu")
    def item_is_menu        (self): return self._get_bool_prop  ("ItemIsMenu")
    def icon_theme_path     (self): return self._get_string_prop("IconThemePath", None)
    def icon_name           (self): return self._get_string_prop("IconName", None)
    def icon_pixmap         (self): return self._get_pixmap_array_prop("IconPixmap", None)
    def att_icon_name       (self): return self._get_string_prop("AttentionIconName", None)
    def att_icon_pixmap     (self): return self._get_pixmap_array_prop("AttentionIconPixmap", None)
    def overlay_icon_name   (self): return self._get_string_prop("OverlayIconName", None)
    def overlay_icon_pixmap (self): return self._get_pixmap_array_prop("OverlayIconPixmap", None)
    def tooltip             (self):
        # For now only appindicator seems to provide anything remotely like a tooltip
        if self.path.startswith(APPINDICATOR_PATH_PREFIX):
            return self._get_string_prop("XAyatanaLabel")
        else:
            # For everything else, no tooltip
            return ""

    def activate(self, button, x, y):
        if button == Gdk.BUTTON_PRIMARY:
            try:
                args = GLib.Variant("(ii)", (x, y))
                # This sucks, nothing is consistent.  Most programs don't have a primary
                # activate (all appindicator ones).  One that I checked that does, claims
                # (according to proxyinfo.get_method_info()) it only accepts SecondaryActivate,
                # but only listens for "Activate", so we attempt a sync primary call, and async
                # secondary if needed.  Otherwise we're waiting for the first to finish in a
                # callback before we can try the secondary.  Maybe we just call secondary alwayS??
                self._call_sn_item_method_sync("Activate", args)
            except GLib.Error:
                self._call_sn_item_method("SecondaryActivate", args)
        elif button == Gdk.BUTTON_MIDDLE:
            self._call_sn_item_method("SecondaryActivate", args)

    def show_context_menu(self, button, x, y):
        if button == Gdk.BUTTON_SECONDARY:
            args = GLib.Variant("(ii)", (x, y))
            self._call_sn_item_method("ContextMenu", args)

    def scroll(self, delta, o_str):
        args = GLib.Variant("(is)", (delta, o_str))
        self._call_sn_item_method("Scroll", args)

    def destroy(self):
        try:
            self._session.signal_unsubscribe(self.sn_subscription_id)
            # self._session.close()
            self._session = None
        except Exception as e:
            print(str(e))
