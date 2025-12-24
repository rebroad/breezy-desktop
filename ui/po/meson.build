# Collect all source files that may contain translatable strings
python_sources = []
ui_sources = []

# Python files
foreach file : [
  '../modules/PyXRLinuxDriverIPC/xrdriveripc.py',
  '../src/__init__.py',
  '../src/configmanager.py',
  '../src/connecteddevice.py',
  '../src/customresolutiondialog.py',
  '../src/customresolutiondialogcontent.py',
  '../src/displaydistancedialog.py',
  '../src/displaydistancedialogcontent.py',
  '../src/extensionsmanager.py',
  '../src/failedverification.py',
  '../src/files.py',
  '../src/main.py',
  '../src/nodevice.py',
  '../src/nodriver.py',
  '../src/noextension.py',
  '../src/settingsmanager.py',
  '../src/shortcutdialog.py',
  '../src/statemanager.py',
  '../src/time.py',
  '../src/verify.py',
  '../src/virtualdisplay.py',
  '../src/virtualdisplaymanager.py',
  '../src/virtualdisplayrow.py',
  '../src/window.py',
]
  python_sources += file
endforeach

# UI files
foreach file : [
  '../src/gtk/connected-device.ui',
  '../src/gtk/custom-resolution-dialog-content.ui',
  '../src/gtk/custom-resolution-dialog.ui',
  '../src/gtk/display-distance-dialog-content.ui',
  '../src/gtk/display-distance-dialog.ui',
  '../src/gtk/failed-verification.ui',
  '../src/gtk/no-device.ui',
  '../src/gtk/no-driver.ui',
  '../src/gtk/no-extension.ui',
  '../src/gtk/shortcut-dialog.ui',
  '../src/gtk/virtual-display-row.ui',
  '../src/gtk/window.ui',
]
  ui_sources += file
endforeach

all_sources = python_sources + ui_sources

# Custom target to update .pot file only when source files change
# This script checks timestamps and only updates if needed
update_pot_script = find_program('update_pot_if_needed.sh', dirs: meson.current_source_dir())
update_pot = custom_target('update-pot-if-needed',
  input: all_sources,
  output: 'breezydesktop.pot.timestamp',
  command: [update_pot_script, '@OUTPUT@'],
  # Touch the output file to track when we last checked
  build_always_stale: false,
  build_by_default: false,
  install: false,
)

# Use meson's i18n.gettext for .po compilation and .mo generation
# This will use the existing .pot file (updated by our custom target if needed)
i18n.gettext('breezydesktop', preset: 'glib')
