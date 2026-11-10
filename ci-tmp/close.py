"""TEMPORARY (test run of the release workflow).

Asks the LegacyEngine window to close in the way a window manager does when its
close button is clicked: a WM_DELETE_WINDOW client message.
"""
import sys

from Xlib import X, display
from Xlib.protocol import event


def find(window, name):
    for child in window.query_tree().children:
        if child.get_wm_name() == name:
            return child
        found = find(child, name)
        if found is not None:
            return found
    return None


def close_message(window, protocols, delete):
    return event.ClientMessage(window=window, client_type=protocols,
                               data=(32, [delete, X.CurrentTime, 0, 0, 0]))


if __name__ == "__main__":
    d = display.Display()
    window = find(d.screen().root, "LegacyEngine")
    if window is None:
        sys.exit("window not found")
    protocols = d.intern_atom("WM_PROTOCOLS")
    delete = d.intern_atom("WM_DELETE_WINDOW")
    geometry = window.get_geometry()
    print(f"window {window.id:#x} at {geometry.x},{geometry.y} size {geometry.width}x{geometry.height};",
          "WM_DELETE_WINDOW in WM_PROTOCOLS:", delete in list(window.get_wm_protocols()))
    window.send_event(close_message(window, protocols, delete), event_mask=X.NoEventMask)
    d.sync()
    print("close request sent")
