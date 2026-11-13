import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('reference', ROOT / 'docs/generate_reference.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


class ReferenceTests(unittest.TestCase):
    def test_reference_current_and_every_example_present(self):
        content = reference.render_reference()
        self.assertEqual((ROOT / 'docs/scripting-reference.md').read_text(), content)
        entries, _ = reference.inventory()
        self.assertGreater(len(entries), 150)
        for entry in entries:
            self.assertIn('### ' + entry['key'] + '\n', content)
            self.assertTrue(reference.example(entry).strip())

    def test_runtime_scene_methods_covered(self):
        source = (ROOT / 'src/scripting/lua_scene.cpp').read_text()
        entries, _ = reference.inventory()
        names = {entry['name'] for entry in entries}
        for array in re.findall(r'(?:objectMethods|cameraMethods|cameraFunctions|methods|names)\[\]\s*\{(.*?)\};', source, re.S):
            for name in re.findall(r'"(\w+)"', array):
                self.assertIn(name, names)

    def test_release_ships_reference(self):
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        self.assertIn('cp -r build/docs "$package/docs"', workflow)
        self.assertIn('*.html', workflow)
        app = (ROOT / 'src/app/main.cpp').read_text()
        self.assertIn('openManual("scripting-reference.html")', app)
        self.assertIn('SDL_GetBasePath()', app)

    def test_new_runtime_contracts_are_documented(self):
        content = reference.render_reference()
        for name in ('onMouseDown', 'onMouseUp', 'onMouseEnter', 'onMouseExit', 'onMouseOver',
                     'onAudioStarted', 'onAudioFinished', 'onAudioPaused', 'onAudioResumed', 'onAudioStopped',
                     'waitForSeconds', 'wait', 'GameObject.setActive', 'GameObject.isActive'):
            self.assertIn('### ' + name + '\n', content)
            self.assertTrue(reference.CALLABLE_DETAILS[name])
        self.assertIn('## Callback and wait quick guide', content)
        self.assertIn('opaque `AudioEvent` tokens', content)
        self.assertIn('custom/level event listeners', content)

    def test_level_actions_use_file_menu(self):
        projects = (ROOT / 'docs/projects.md').read_text()
        self.assertIn('**File > New Level...**', projects)
        self.assertIn('**File > Save Level**', projects)
        self.assertNotIn('Project > Levels > New Level', projects)
