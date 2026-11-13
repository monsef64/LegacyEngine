import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('docs_build', Path(__file__).resolve().parents[1] / 'build.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class DocumentationTests(unittest.TestCase):
    def test_build_offline_site(self):
        with tempfile.TemporaryDirectory() as temp:
            pages = builder.build(Path(temp), 'test-revision')
            self.assertGreaterEqual(len(pages), 10)
            self.assertIn('index.html', pages)
            self.assertIn('scripting.html', pages)
            self.assertIn('scripting-reference.html', pages)
            self.assertIn('href="scripting-reference.html"', pages['index.html'])
            self.assertIn('fadeSkybox', pages['scripting-reference.html'])
            self.assertTrue((Path(temp) / 'scripting-reference.html').is_file())
            self.assertIn('<pre class="highlight"><code class="language-lua">', pages['scripting.html'])
            self.assertIn('aria-current="page"', pages['index.html'])
            self.assertIn('href="building.html"', pages['index.html'])
            self.assertIn('/blob/test-revision/', pages['building.html'])
            self.assertNotIn('href="projects.md"', pages['index.html'])
            for asset in ('style.css', 'screenshot.png', 'LICENSE'):
                self.assertTrue((Path(temp) / asset).is_file())
            self.assertEqual(pages, builder.build(Path(temp), 'test-revision'))

    def test_offline_syntax_highlighting(self):
        with tempfile.TemporaryDirectory() as temp:
            pages = builder.build(Path(temp))
            reference = pages['scripting-reference.html']
            self.assertIn('<span class="kr">function</span>', reference)
            self.assertIn('class="language-legacyscript"', pages['prefabs.html'])
            self.assertIn('.highlight .', (Path(temp) / 'style.css').read_text())
            self.assertNotIn('<script', reference)

    def test_reject_source_overwrite(self):
        for target in (builder.REPO, builder.SOURCE, builder.REPO / 'src', builder.REPO.parent):
            with self.assertRaises(ValueError):
                builder.build(target)

    def test_validate_links(self):
        with self.assertRaisesRegex(ValueError, 'broken local link'):
            builder.validate({'index.html': '<a href="missing.html">Broken</a>'}, set())
        with self.assertRaisesRegex(ValueError, 'missing anchor'):
            builder.validate({'index.html': '<a href="#missing">Broken</a>'}, set())
        builder.validate({'index.html': '<h1 id="test">Test</h1><a href="#test">OK</a>'}, set())

    def test_invalid_navigation(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'source'
            source.mkdir()
            (source / 'navigation.json').write_text('[{"file":"missing.md","title":"Missing"}]')
            with patch.object(builder, 'SOURCE', source):
                with self.assertRaisesRegex(ValueError, 'Missing documentation source'):
                    builder.build(Path(temp) / 'output')
                (source / 'index.md').write_text('# Overview')
                (source / 'navigation.json').write_text('[{"file":"index.md","title":"Overview"},{"file":"index.md","title":"Duplicate"}]')
                with self.assertRaisesRegex(ValueError, 'duplicate documentation page'):
                    builder.build(Path(temp) / 'output')

    def test_no_remote_runtime_resources(self):
        with tempfile.TemporaryDirectory() as temp:
            for content in builder.build(Path(temp)).values():
                self.assertNotIn('<script', content)
                self.assertNotIn('src="https:', content)
                self.assertNotIn('<link rel="stylesheet" href="https:', content)


if __name__ == '__main__':
    unittest.main()
