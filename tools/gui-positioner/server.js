const http = require('node:http');
const fs = require('node:fs/promises');
const path = require('node:path');

const repoRoot = path.resolve(__dirname, '..', '..');
const publicDir = path.join(__dirname, 'public');
const guiDir = path.join(repoRoot, 'resources', 'gui');
const layoutPath = path.join(guiDir, 'orch_gui_layout.json');
const port = Number(process.env.ORCH_GUI_POSITIONER_PORT || 4173);

const contentTypes = {
  '.css': 'text/css; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png'
};

function send(response, status, body, type = 'text/plain; charset=utf-8') {
  response.writeHead(status, { 'content-type': type, 'cache-control': 'no-store' });
  response.end(body);
}

async function sendFile(response, filename) {
  try {
    const data = await fs.readFile(filename);
    send(response, 200, data, contentTypes[path.extname(filename)] || 'application/octet-stream');
  } catch {
    send(response, 404, 'Not found');
  }
}

function validLayout(layout) {
  return layout && typeof layout === 'object'
    && layout.canvas && layout.canvas.width === 540 && layout.canvas.height === 430
    && layout.groups && typeof layout.groups === 'object'
    && layout.elements && typeof layout.elements === 'object';
}

async function readBody(request) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > 1024 * 1024) throw new Error('Layout payload is too large');
    chunks.push(chunk);
  }
  return Buffer.concat(chunks).toString('utf8');
}

const server = http.createServer(async (request, response) => {
  const url = new URL(request.url, `http://${request.headers.host}`);
  if (request.method === 'GET' && url.pathname === '/api/layout') {
    return sendFile(response, layoutPath);
  }
  if (request.method === 'PUT' && url.pathname === '/api/layout') {
    try {
      const layout = JSON.parse(await readBody(request));
      if (!validLayout(layout)) return send(response, 400, 'Invalid layout payload');
      const temporaryPath = `${layoutPath}.tmp`;
      await fs.writeFile(temporaryPath, `${JSON.stringify(layout, null, 2)}\n`, 'utf8');
      await fs.rename(temporaryPath, layoutPath);
      return send(response, 200, JSON.stringify({ saved: true }), 'application/json; charset=utf-8');
    } catch (error) {
      return send(response, 400, error.message || 'Unable to save layout');
    }
  }
  if (request.method === 'GET' && url.pathname.startsWith('/asset/')) {
    const name = path.basename(decodeURIComponent(url.pathname.slice('/asset/'.length)));
    return sendFile(response, path.join(guiDir, name));
  }
  if (request.method === 'GET') {
    const requested = url.pathname === '/' ? 'index.html' : url.pathname.slice(1);
    const filename = path.resolve(publicDir, requested);
    if (!filename.startsWith(`${publicDir}${path.sep}`) && filename !== path.join(publicDir, 'index.html')) {
      return send(response, 403, 'Forbidden');
    }
    return sendFile(response, filename);
  }
  return send(response, 405, 'Method not allowed');
});

server.listen(port, '127.0.0.1', () => {
  console.log(`Orch GUI positioner: http://127.0.0.1:${port}`);
});
