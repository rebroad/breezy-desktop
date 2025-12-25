import os
import pydbus
from gi.repository import GObject

BREEZY_DESKTOP_UUID = "breezydesktop@xronlinux.com"
EXTENSION_STATE_ENABLED = 1

def detect_desktop_environment():
    """Detect the current desktop environment."""
    desktop = os.environ.get('XDG_CURRENT_DESKTOP', '').lower()
    session = os.environ.get('XDG_SESSION_DESKTOP', '').lower()

    if 'xfce' in desktop or 'xfce' in session:
        return 'x11'
    elif 'gnome' in desktop or 'gnome' in session:
        return 'gnome'
    elif 'kde' in desktop or 'plasma' in desktop or 'kde' in session:
        return 'kde'

    # Fallback: check for running processes
    try:
        import subprocess
        result = subprocess.run(['pgrep', '-x', 'xfce4-session'], capture_output=True)
        if result.returncode == 0:
            return 'x11'
        result = subprocess.run(['pgrep', '-x', 'gnome-session'], capture_output=True)
        if result.returncode == 0:
            return 'gnome'
        result = subprocess.run(['pgrep', '-x', 'startplasma'], capture_output=True)
        if result.returncode == 0:
            return 'kde'
    except:
        pass

    return 'unknown'

class ExtensionsManager(GObject.GObject):
    __gproperties__ = {
        'breezy-enabled': (bool, 'Breezy Enabled', 'Whether the Breezy Desktop extension/backend is enabled', False, GObject.ParamFlags.READWRITE)
    }

    _instance = None

    @staticmethod
    def get_instance():
        if ExtensionsManager._instance is None:
            ExtensionsManager._instance = ExtensionsManager()
        return ExtensionsManager._instance

    def __init__(self):
        GObject.GObject.__init__(self)

        self.desktop_env = detect_desktop_environment()
        self.gnome_shell_extensions = None
        self.bus = None

        if self.desktop_env == 'gnome':
            try:
                self.bus = pydbus.SessionBus()
                self.gnome_shell_extensions = self.bus.get("org.gnome.Shell.Extensions")
                self.gnome_shell_extensions.ExtensionStateChanged.connect(self._handle_extension_state_change)
                self.remote_extension_state = self.is_enabled()
            except Exception as e:
                # GNOME Shell not available
                self.remote_extension_state = False
        elif self.desktop_env == 'x11':
            # X11-based desktops don't use extensions, backend is always "enabled" if available
            self.remote_extension_state = True
        else:
            self.remote_extension_state = False

    def _handle_extension_state_change(self, extension_uuid, state):
        if extension_uuid == BREEZY_DESKTOP_UUID:
            self.remote_extension_state = state.get('state') == EXTENSION_STATE_ENABLED
            self.set_property('breezy-enabled', self.remote_extension_state)

    def is_installed(self):
        if self.desktop_env == 'x11':
            # For X11-based desktops, check if backend is available
            try:
                from breezydesktop.x11 import X11Backend
                backend = X11Backend()
                return backend.is_available()
            except ImportError:
                return False
        elif self.desktop_env == 'gnome':
            return self._is_installed(BREEZY_DESKTOP_UUID)
        else:
            return False

    def enable(self):
        if self.desktop_env == 'gnome':
            self._enable_extension(BREEZY_DESKTOP_UUID)
        elif self.desktop_env == 'x11':
            # X11 backend doesn't need enabling, just mark as enabled
            self.remote_extension_state = True
            self.set_property('breezy-enabled', True)

    def disable(self):
        if self.desktop_env == 'gnome':
            self._disable_extension(BREEZY_DESKTOP_UUID)
        elif self.desktop_env == 'x11':
            self.remote_extension_state = False
            self.set_property('breezy-enabled', False)

    def is_enabled(self):
        if self.desktop_env == 'x11':
            return self.remote_extension_state
        elif self.desktop_env == 'gnome':
            return self._is_enabled(BREEZY_DESKTOP_UUID)
        else:
            return False

    def _is_installed(self, extension_uuid):
        if not self.gnome_shell_extensions:
            return False
        extensions_result = self.gnome_shell_extensions.ListExtensions()
        for extension in extensions_result:
            if extension == extension_uuid:
                return True
        
        return False

    def _enable_extension(self, extension_uuid):
        if not self.gnome_shell_extensions:
            return
        if not self.gnome_shell_extensions.UserExtensionsEnabled:
            self.gnome_shell_extensions.UserExtensionsEnabled = True

        self.gnome_shell_extensions.EnableExtension(extension_uuid)

    def _disable_extension(self, extension_uuid):
        if not self.gnome_shell_extensions:
            return
        self.gnome_shell_extensions.DisableExtension(extension_uuid)

    def _is_enabled(self, extension_uuid):
        if not self.gnome_shell_extensions:
            return False
        return self.gnome_shell_extensions.GetExtensionInfo(extension_uuid).get('state') == EXTENSION_STATE_ENABLED

    def do_set_property(self, prop, value):
        if prop.name == 'breezy-enabled' and value != self.remote_extension_state:
            self.enable() if value == True else self.disable()

    def do_get_property(self, prop):
        if prop.name == 'breezy-enabled':
            return self.remote_extension_state
