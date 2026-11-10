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

http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || "localhost"}`);

  if (url.pathname === "/download") {
    const name = url.searchParams.get("file");
    if (!name || name !== path.basename(name)) {
      res.writeHead(400).end("Invalid filename");
      return;
    }

    const file = path.join(root, name);
    try {
      const stat = fs.lstatSync(file);
      if (!stat.isFile()) throw new Error();
      res.setHeader("Content-Disposition", `attachment; filename*=UTF-8''${encodeURIComponent(name)}`);
      res.setHeader("Content-Length", stat.size);
      fs.createReadStream(file).pipe(res);
    } catch {
      res.writeHead(404).end("File not found");
    }
    return;
  }

  if (url.pathname !== "/") {
    res.writeHead(404).end("Not found");
    return;
  }

  let files;
  try {
    files = fs.readdirSync(root, { withFileTypes: true })
      .filter((entry) => entry.isFile())
      .map((entry) => {
        const stat = fs.statSync(path.join(root, entry.name));
        return { name: entry.name, size: stat.size };
      })
      .sort((a, b) => a.name.localeCompare(b.name));
  } catch {
    res.writeHead(500).end("Could not read folder");
    return;
  }

  const rows = files.map((file) => `
    <a class="file" href="/download?file=${encodeURIComponent(file.name)}">
      <span class="icon">↓</span>
      <span class="name">${escapeHtml(file.name)}</span>
      <span class="size">${formatSize(file.size)}</span>
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
  .sub { color: #9aa8c7; margin: 0 0 24px; }
  .file { display: flex; align-items: center; gap: 14px; padding: 16px;
          margin: 10px 0; border: 1px solid #26324e; border-radius: 12px;
          background: #131b30; color: inherit; text-decoration: none; }
  .file:hover { border-color: #7c9cff; background: #192440; }
  .icon { display: grid; place-items: center; width: 38px; height: 38px;
          border-radius: 10px; background: #263b70; color: #a9c0ff; font-size: 22px; }
  .name { flex: 1; overflow-wrap: anywhere; }
  .size { color: #9aa8c7; font-size: 14px; white-space: nowrap; }
  .empty { color: #9aa8c7; padding: 20px 0; }
</style>
</head>
<body><main>
  <h1>File downloads</h1>
  <p class="sub">${files.length} file${files.length === 1 ? "" : "s"}</p>
  ${rows || '<p class="empty">No files in this folder.</p>'}
</main></body></html>`);
}).listen(port, "0.0.0.0", () => {
  console.log(`Serving ${root} on port ${port}`);
});
