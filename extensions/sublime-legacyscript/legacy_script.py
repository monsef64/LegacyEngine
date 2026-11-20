"""Sublime Text 4 API hover help; catalogs generated from engine definitions."""
import html
import json
import sublime
import sublime_plugin


class LegacyScriptHelp(sublime_plugin.EventListener):
    def on_hover(self, view, point, hover_zone):
        if hover_zone != sublime.HOVER_TEXT or not view.match_selector(point, "source.legacyscript - comment - string"):
            return
        name = view.substr(view.word(point))
        resource = __name__.split(".")[0]
        try:
            entries = json.loads(sublime.load_resource("Packages/" + resource + "/api.json"))
            help_text = next((item["signature"] for item in entries if item["name"] == name), None)
            if help_text:
                view.show_popup("<body><b>LegacyScript</b><p>" + html.escape(help_text) + "</p></body>",
                                sublime.HIDE_ON_MOUSE_MOVE_AWAY, point, max_width=640)
        except (ValueError, OSError):
            pass
