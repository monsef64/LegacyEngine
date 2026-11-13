#!/usr/bin/env python3
"""Build the offline LegacyEngine manual. Requires docs/requirements.txt."""
import argparse
import html
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import shutil
from urllib.parse import quote, unquote, urlsplit

import markdown
from pygments import highlight
from pygments.lexers import get_lexer_by_name
from pygments.formatters import HtmlFormatter
from pygments.util import ClassNotFound
from pygments.lexers.scripting import LuaLexer
from pygments.lexer import inherit
from pygments.token import Comment


class LegacyScriptLexer(LuaLexer):
    tokens = {'root': [(r'(?m)^\s*@export[^\n]*', Comment.Preproc), inherit]}

SOURCE = Path(__file__).resolve().parent
REPO = SOURCE.parent


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.links = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.add(attrs['id'])
        for name in ('href', 'src'):
            if name in attrs:
                self.links.append(attrs[name])


def validate(pages, assets):
    parsed = {}
    for name, content in pages.items():
        parsed[name] = Links()
        parsed[name].feed(content)
    for name, parser in parsed.items():
        for link in parser.links:
            url = urlsplit(link)
            if url.scheme or url.netloc:
                continue
            target = unquote(url.path) or name
            if target not in pages and target not in assets:
                raise ValueError(f'{name}: broken local link {link}')
            if url.fragment and target in parsed and unquote(url.fragment) not in parsed[target].ids:
                raise ValueError(f'{name}: missing anchor {link}')


def build(output, revision='main'):
    output = Path(output).resolve()
    # Never allow output to overwrite repository sources, or an ancestor of them.
    if output == REPO or output in REPO.parents or (output.is_relative_to(REPO) and not output.is_relative_to(REPO / 'build')):
        raise ValueError('Output must be under build/ or outside the repository')
    navigation = json.loads((SOURCE / 'navigation.json').read_text(encoding='utf-8'))
    seen = set()
    for entry in navigation:
        file = entry['file']
        if not re.fullmatch(r'[a-z0-9-]+\.md', file) or file in seen:
            raise ValueError(f'Invalid or duplicate documentation page: {file}')
        if not (SOURCE / file).is_file():
            raise ValueError(f'Missing documentation source: {file}')
        seen.add(file)
    if 'index.md' not in seen:
        raise ValueError('Documentation requires index.md')
    pages = {}
    esc = html.escape
    for index, entry in enumerate(navigation):
        md = markdown.Markdown(extensions=['fenced_code', 'tables', 'toc', 'sane_lists'], extension_configs={'toc': {'toc_depth': '2-3'}})
        content = md.convert((SOURCE / entry['file']).read_text(encoding='utf-8'))

        def highlight_block(match):
            language = match.group(1)
            alias = 'lua' if language in ('legacyscript', 'ls') else language
            try:
                lexer = LegacyScriptLexer() if alias == 'lua' else get_lexer_by_name(alias)
            except ClassNotFound:
                return match.group(0)
            code = html.unescape(match.group(2))
            colored = highlight(code, lexer, HtmlFormatter(nowrap=True))
            return f'<pre class="highlight"><code class="language-{language}">{colored}</code></pre>'
        content = re.sub(r'<pre><code class="language-([a-zA-Z0-9_+-]+)">(.*?)</code></pre>', highlight_block, content, flags=re.S)

        def rewrite(match):
            link = html.unescape(match.group(2))
            url = urlsplit(link)
            if not url.scheme and not url.netloc:
                if url.path.startswith('../'):
                    link = 'https://github.com/monsef64/LegacyEngine/blob/' + quote(revision, safe='') + '/' + url.path[3:]
                    if url.fragment:
                        link += '#' + url.fragment
                elif url.path.endswith('.md'):
                    link = url.path[:-3] + '.html'
                    if url.fragment:
                        link += '#' + url.fragment
            return match.group(1) + esc(link, quote=True) + match.group(3)

        content = re.sub(r'(\b(?:href|src)=")([^"]*)(")', rewrite, content)
        nav = ''.join(f'<a href="{item["file"][:-3]}.html"' + (' aria-current="page"' if item == entry else '') + f'>{esc(item["title"])}</a>' for item in navigation)
        neighbors = []
        for offset, label in ((-1, '← Previous: '), (1, 'Next: ')):
            if 0 <= index + offset < len(navigation):
                item = navigation[index + offset]
                neighbors.append(f'<a href="{item["file"][:-3]}.html">{label}{esc(item["title"])}</a>')
        toc = '<details class="toc"><summary>On this page</summary>' + md.toc + '</details>' if md.toc_tokens else ''
        pages[entry['file'][:-3] + '.html'] = f'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{esc(entry['title'])} · LegacyEngine</title><link rel="stylesheet" href="style.css"></head>
<body><a class="skip" href="#content">Skip to content</a><div class="layout">
<aside><a class="brand" href="index.html">LegacyEngine</a><p class="subtitle">Engine &amp; LegacyScript manual</p><nav aria-label="Documentation">{nav}</nav></aside>
<main id="content">{toc}<article>{content}</article><div class="page-links">{''.join(neighbors)}</div>
<footer class="footer">LegacyEngine · GPL-2.0-only · Revision {esc(revision)} · Offline documentation</footer></main></div></body></html>
'''
    assets = {'style.css', 'screenshot.png', 'LICENSE'}
    validate(pages, assets)
    output.mkdir(parents=True, exist_ok=True)
    for name in set(pages) | assets:
        if (output / name).is_symlink() or (output / name).is_dir():
            raise ValueError(f'Refusing to overwrite non-regular output: {name}')
    for name, content in pages.items():
        (output / name).write_text(content, encoding='utf-8')
    for name in ('style.css', 'screenshot.png'):
        shutil.copyfile(SOURCE / name, output / name)
        if name == 'style.css':
            with (output / name).open('a', encoding='utf-8') as stylesheet:
                stylesheet.write('\n' + HtmlFormatter(style='monokai').get_style_defs('.highlight'))
    shutil.copyfile(REPO / 'LICENSE', output / 'LICENSE')
    print(f'Built {len(pages)} documentation pages in {output}')
    return pages


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=REPO / 'build' / 'docs')
    parser.add_argument('--revision', default='main', help='Git revision used by repository-source links')
    args = parser.parse_args()
    try:
        build(args.output, args.revision)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f'Documentation build failed: {error}\n')
