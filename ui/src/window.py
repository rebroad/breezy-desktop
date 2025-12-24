# window.py
#
# Copyright 2024 Unknown
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later

from gi.repository import Gtk, GLib
from .extensionsmanager import ExtensionsManager
from .statemanager import StateManager
from .settingsmanager import SettingsManager
from .connecteddevice import ConnectedDevice
from .failedverification import FailedVerification
from .nodevice import NoDevice
from .nodriver import NoDriver
from .noextension import NoExtension
from .verify import verify_installation

@Gtk.Template(resource_path='/com/xronlinux/BreezyDesktop/gtk/window.ui')
class BreezydesktopWindow(Gtk.ApplicationWindow):
    __gtype_name__ = 'BreezydesktopWindow'

    main_content = Gtk.Template.Child()

    def __init__(self, skip_verification, **kwargs):
        super().__init__(**kwargs)

        self.connected_device = ConnectedDevice()
        self.failed_verification = FailedVerification()
        self.no_device = NoDevice()
        self.no_driver = NoDriver()
        self.no_extension = NoExtension()
        
        self._skip_verification = skip_verification

        self.settings = SettingsManager.get_instance().settings
        self.state_manager = StateManager.get_instance()
        self.state_manager.connect('device-update', self._handle_state_update)
        self.settings.connect('changed::debug-no-device', self._handle_settings_update)

        self._handle_state_update(self.state_manager, None)

        self._skip_verification = skip_verification

        self.connect("destroy", self._on_window_destroy)

    def _handle_settings_update(self, settings_manager, key):
        self._handle_state_update(self.state_manager, None)

    def _handle_state_update(self, state_manager, val):
        GLib.idle_add(self._handle_state_update_gui, state_manager)

    def _handle_state_update_gui(self, state_manager):
        for child in self.main_content:
            self.main_content.remove(child)

        if self.settings.get_boolean('debug-no-device'):
            self.main_content.append(self.connected_device)
            self.connected_device.set_device_name('Fake device')
        elif not self._skip_verification and not verify_installation():
            self.main_content.append(self.failed_verification)
        elif not ExtensionsManager.get_instance().is_installed():
            # Only show no-extension for GNOME (XFCE4 doesn't use extensions)
            desktop_env = ExtensionsManager.get_instance().desktop_env if hasattr(ExtensionsManager.get_instance(), 'desktop_env') else 'unknown'
            if desktop_env == 'gnome':
                self.main_content.append(self.no_extension)
            else:
                # For XFCE4 and other desktops, skip extension check
                pass
        elif not self.state_manager.driver_running:
            self.main_content.append(self.no_driver)
        elif not state_manager.connected_device_name:
            self.main_content.append(self.no_device)
        else:
            self.main_content.append(self.connected_device)
            self.connected_device.set_device_name(state_manager.connected_device_name)
        
        self.set_resizable(True)
        self.set_default_size(1, 1)
        
        return False

    def _on_window_destroy(self, widget):
        self.state_manager.disconnect_by_func(self._handle_state_update)