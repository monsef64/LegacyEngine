const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");

const requestedFolder = process.argv[2];
if (!requestedFolder) {
  console.error("Usage: node server.js /path/to/folder");
  process.exit(1);
}

const root = fs.realpathSync(requestedFolder);
const port = Number(process.env.PORT || 8000);

const escapeHtml = (s) =>
  s.replace(/[&<>"']/g, (c) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
  })[c]);

const formatSize = (n) => {
  if (n < 1024) return `${n} B`;
  if (n < 1024 ** 2) return `${(n / 1024).toFixed(1)} KB`;
  if (n < 1024 ** 3) return `${(n / 1024 ** 2).toFixed(1)} MB`;
  return `${(n / 1024 ** 3).toFixed(2)} GB`;
};

const makeHref = (segments, isDir = false) =>
  "/" + segments.map(encodeURIComponent).join("/") + (isDir && segments.length ? "/" : "");

function resolveSafe(segments) {
  let current = root;

  for (let i = 0; i < segments.length; i++) {
    current = path.join(current, segments[i]);
    const stat = fs.lstatSync(current);

    // Don't follow symlinks outside the shared folder.
    if (stat.isSymbolicLink()) throw new Error("Symlinks are not served");
    if (i < segments.length - 1 && !stat.isDirectory()) {
      throw new Error("Not a directory");
    }
  }

  return current;
}

http.createServer((req, res) => {
  let segments;

  try {
    segments = new URL(req.url, "http://localhost").pathname
      .split("/")
      .filter(Boolean)
      .map(decodeURIComponent);

    if (segments.some((s) => !s || s === "." || s === ".." || s.includes("/") || s.includes("\\"))) {
      throw new Error("Invalid path");
    }
  } catch {
    res.writeHead(400).end("Invalid path");
    return;
  }

  let target;
  try {
    target = resolveSafe(segments);
  } catch {
    res.writeHead(404).end("Not found");
    return;
  }

  let stat;
  try {
    stat = fs.lstatSync(target);
  } catch {
    res.writeHead(404).end("Not found");
    return;
  }

  if (stat.isFile()) {
    res.writeHead(200, {
      "Content-Disposition": `attachment; filename*=UTF-8''${encodeURIComponent(path.basename(target))}`,
      "Content-Length": stat.size,
      "X-Content-Type-Options": "nosniff"
    });
    fs.createReadStream(target).pipe(res);
    return;
  }

  if (!stat.isDirectory()) {
    res.writeHead(404).end("Not found");
    return;
  }

  let entries;
  try {
    entries = fs.readdirSync(target, { withFileTypes: true })
      .filter((entry) => !entry.isSymbolicLink())
      .map((entry) => {
        const childSegments = [...segments, entry.name];
        const childPath = path.join(target, entry.name);
        const childStat = fs.lstatSync(childPath);
        return {
          name: entry.name,
          isDir: childStat.isDirectory(),
          size: childStat.isFile() ? childStat.size : null,
          href: makeHref(childSegments, childStat.isDirectory())
        };
      })
      .sort((a, b) => Number(b.isDir) - Number(a.isDir) || a.name.localeCompare(b.name));
  } catch {
    res.writeHead(500).end("Could not read folder");
    return;
  }

  const parentHref = makeHref(segments.slice(0, -1), true);
  const rows = entries.map((entry) => `
    <a class="file" href="${escapeHtml(entry.href)}">
      <span class="icon">${entry.isDir ? "📁" : "↓"}</span>
      <span class="name">${escapeHtml(entry.name)}${entry.isDir ? "/" : ""}</span>
      <span class="size">${entry.isDir ? "Folder" : formatSize(entry.size)}</span>
    </a>`).join("");

  res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
  res.end(`<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>File downloads</title>
<style>
  * { box-sizing: border-box; }
  body { margin: 0; padding: 48px 20px; background: #0b1020; color: #edf2ff;
         font: 16px system-ui, sans-serif; }
  main { max-width: 760px; margin: auto; }
  h1 { margin-bottom: 8px; font-size: 2rem; }
  .sub { color: #9aa8c7; margin: 0 0 24px; overflow-wrap: anywhere; }
  .file { display: flex; align-items: center; gap: 14px; padding: 16px;
          margin: 10px 0; border: 1px solid #26324e; border-radius: 12px;
          background: #131b30; color: inherit; text-decoration: none; }
  .file:hover { border-color: #7c9cff; background: #192440; }
  .icon { width: 38px; text-align: center; font-size: 22px; }
  .name { flex: 1; overflow-wrap: anywhere; }
  .size { color: #9aa8c7; font-size: 14px; white-space: nowrap; }
  .empty { color: #9aa8c7; padding: 20px 0; }
</style>
</head>
<body><main>
  <h1>File downloads</h1>
  <p class="sub">/${escapeHtml(segments.join("/")) || ""}</p>
  ${segments.length ? `<a class="file" href="${escapeHtml(parentHref)}">⬆ <span class="name">Up one folder</span></a>` : ""}
  ${rows || '<p class="empty">This folder is empty.</p>'}
</main></body></html>`);
}).listen(port, "0.0.0.0", () => {
  console.log(`Serving ${root} on port ${port}`);
});
