const express = require('express');
const fs = require('fs/promises');
const { constants: fsConstants } = require('fs');
const path = require('path');
const { execFile } = require('child_process');
const SkewGeometry = require('./public/skew-geometry');

const PORT = process.env.PORT || 3000;
const FOOTAGE_DIR = process.env.FOOTAGE_DIR || '/footage';
const FINAL_DIR = path.join(FOOTAGE_DIR, 'final');
const SCREEN_OUTPUTS_FILE = path.join(__dirname, 'public', 'screen-outputs.json');

const app = express();
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// Raw footage files (supports range requests for video seeking)
app.use('/video', express.static(FOOTAGE_DIR));

function isValidFile(file) {
  return typeof file === 'string' && file !== '' && path.basename(file) === file;
}

// Folders a clip can be read from: raw ('' = top level), final, or a screen
// size folder such as 368-448
function isValidFolder(folder) {
  return folder === '' || folder === 'final' || /^\d+-\d+$/.test(folder);
}

for (const page of ['preview', 'trim', 'crop', 'skew']) {
  app.get(`/${page}`, (req, res) => {
    res.sendFile(path.join(__dirname, 'public', `${page}.html`));
  });
}

function parseRate(rate) {
  const [num, den] = String(rate || '').split('/').map(Number);
  return num && den ? num / den : NaN;
}

// Screen-size exports are MJPEG for the ESP32 player (src/src/video_player.cpp):
// concatenated JPEG frames with no audio and no timing, played back at a fixed
// rate. ESP_FPS must match STARTUP_VIDEO_FPS in the firmware, and stay below
// what the device sustains (~17fps at 448x368) or clips play in slow motion.
// 15 is an even divisor of 30/60fps sources, so frames are dropped evenly.
const ESP_EXT = '.mjpeg';
const ESP_FPS = 15;
const espName = (name) => path.basename(name, path.extname(name)) + ESP_EXT;

// Each MJPEG frame starts with a JPEG SOI marker (FF D8 FF); 0xFF bytes in
// entropy-coded data are always stuffed (FF 00), so this can't false-match.
function countMjpegFrames(buf) {
  let count = 0;
  for (let i = buf.indexOf(0xff); i !== -1 && i + 2 < buf.length; i = buf.indexOf(0xff, i + 1)) {
    if (buf[i + 1] === 0xd8 && buf[i + 2] === 0xff) count++;
  }
  return count;
}

async function probe(file, folder = '') {
  const info = await ffprobe(file, folder);
  if (path.extname(file) !== ESP_EXT) return info;
  // ffprobe can't time a raw MJPEG stream: count frames, at the ESP's rate
  const frames = countMjpegFrames(await fs.readFile(path.join(FOOTAGE_DIR, folder, file)));
  return { ...info, fps: ESP_FPS, frames, duration: frames / ESP_FPS };
}

function ffprobe(file, folder) {
  const args = [
    '-v', 'error',
    '-select_streams', 'v:0',
    '-show_entries', 'stream=width,height,r_frame_rate,avg_frame_rate,nb_frames:format=duration',
    '-of', 'json',
    path.join(FOOTAGE_DIR, folder, file),
  ];
  return new Promise((resolve, reject) => {
    execFile('ffprobe', args, (err, stdout, stderr) => {
      if (err) return reject(new Error(stderr.trim() || err.message));
      const info = JSON.parse(stdout);
      const stream = (info.streams && info.streams[0]) || {};
      const duration = parseFloat(info.format && info.format.duration);
      const fps = parseRate(stream.r_frame_rate) || parseRate(stream.avg_frame_rate);
      const frames = parseInt(stream.nb_frames, 10) || Math.floor(duration * fps);
      resolve({ file, width: stream.width, height: stream.height, duration, fps, frames });
    });
  });
}

// Files (not folders) directly in `dir`, most recently modified first
async function listFiles(dir) {
  const entries = await fs.readdir(dir, { withFileTypes: true });
  const names = entries
    .filter((e) => e.isFile() && !e.name.startsWith('.'))
    .map((e) => e.name);
  const withTimes = await Promise.all(
    names.map(async (name) => {
      const { mtimeMs } = await fs.stat(path.join(dir, name));
      return { name, mtimeMs };
    })
  );
  return withTimes
    .sort((a, b) => b.mtimeMs - a.mtimeMs)
    .map((f) => f.name);
}

async function exists(p) {
  try {
    await fs.access(p);
    return true;
  } catch {
    return false;
  }
}

app.get('/api/files', async (req, res) => {
  try {
    res.json({ dir: FOOTAGE_DIR, files: await listFiles(FOOTAGE_DIR) });
  } catch (err) {
    res.status(500).json({ error: `Cannot read ${FOOTAGE_DIR}: ${err.message}` });
  }
});

// Screen sizes every final clip is exported at. Re-read on each use so edits
// to the JSON file apply without a restart.
async function loadScreenOutputs() {
  const sizes = JSON.parse(await fs.readFile(SCREEN_OUTPUTS_FILE, 'utf8'));
  const valid = Array.isArray(sizes) && sizes.every((s) =>
    Number.isInteger(s.width) && Number.isInteger(s.height) && s.width > 0 && s.height > 0);
  if (!valid) {
    throw new Error('screen-outputs.json must be an array of { "width": N, "height": N }');
  }
  return sizes;
}

// e.g. <footage>/368-448
const sizeDir = ({ width, height }) => path.join(FOOTAGE_DIR, `${width}-${height}`);

app.get('/api/finals', async (req, res) => {
  try {
    const sizes = await loadScreenOutputs();
    const names = (await exists(FINAL_DIR)) ? await listFiles(FINAL_DIR) : [];
    const finals = await Promise.all(names.map(async (name) => ({
      name,
      sizes: await Promise.all(sizes.map(async (s) => ({
        ...s,
        file: espName(name),
        exists: await exists(path.join(sizeDir(s), espName(name))),
      }))),
    })));
    res.json({ sizes, finals });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

app.get('/api/probe', (req, res) => {
  const { file, folder = '' } = req.query;
  if (!isValidFile(file) || !isValidFolder(folder)) {
    return res.status(400).json({ error: 'Invalid file' });
  }
  probe(file, folder)
    .then((info) => res.json(info))
    .catch((err) => res.status(500).json({ error: err.message }));
});

class HttpError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

// Validates the source file and the requested output name (adding the source's
// extension if none was given), refusing to overwrite an existing file in `dir`.
async function prepareOutput(file, output, dir = FOOTAGE_DIR) {
  if (!isValidFile(file)) throw new HttpError(400, 'Invalid file');
  output = typeof output === 'string' ? output.trim() : '';
  if (!isValidFile(output) || output.startsWith('.')) {
    throw new HttpError(400, 'Invalid new filename');
  }
  if (!path.extname(output)) output += path.extname(file) || '.mp4';
  const outPath = path.join(dir, output);
  if (await exists(outPath)) {
    throw new HttpError(409, `${output} already exists in ${path.relative(FOOTAGE_DIR, dir) || 'raw'}`);
  }
  return { output, outPath };
}

function runFfmpeg(args) {
  return new Promise((resolve, reject) => {
    execFile('ffmpeg', ['-v', 'error', '-n', ...args], { maxBuffer: 10 * 1024 * 1024 }, (err, stdout, stderr) => {
      if (err) reject(new HttpError(500, stderr.trim() || err.message));
      else resolve();
    });
  });
}

function sendError(res, err) {
  res.status(err.status || 500).json({ error: err.message });
}

// Trim to an inclusive frame range [startFrame, endFrame], re-encoding so the
// cut lands on exact frames rather than the nearest keyframe.
app.post('/api/trim', async (req, res) => {
  const { file, output: requested, startFrame, endFrame } = req.body || {};
  try {
    const { output, outPath } = await prepareOutput(file, requested);
    if (!Number.isInteger(startFrame) || !Number.isInteger(endFrame) || startFrame < 0 || endFrame < startFrame) {
      throw new HttpError(400, 'Invalid start/end frame');
    }
    const info = await probe(file);
    if (!info.fps) throw new HttpError(500, 'Could not determine frame rate');
    if (info.frames && endFrame >= info.frames) {
      throw new HttpError(400, `End frame is past the last frame (${info.frames - 1})`);
    }

    const frameCount = endFrame - startFrame + 1;
    // Seek to the start frame's timestamp, rounded down to the microsecond so
    // float rounding can't land just past it. ffmpeg's accurate seek decodes
    // from the previous keyframe and discards everything before this point.
    const seek = Math.floor((startFrame / info.fps) * 1e6) / 1e6;
    await runFfmpeg([
      '-ss', seek.toFixed(6),
      '-i', path.join(FOOTAGE_DIR, file),
      '-frames:v', String(frameCount),
      '-af', `atrim=duration=${(frameCount / info.fps).toFixed(6)}`,
      '-c:v', 'libx264', '-crf', '18', '-preset', 'fast', '-pix_fmt', 'yuv420p',
      '-c:a', 'aac', '-b:a', '192k',
      '-movflags', '+faststart',
      outPath,
    ]);
    res.json({ ok: true, output, frames: frameCount, message: `Saved ${output} (${frameCount} frames)` });
  } catch (err) {
    sendError(res, err);
  }
});

// Crop to the rectangle (x, y, width, height) in source pixels. Values must be
// even because H.264 with 4:2:0 chroma needs even dimensions.
app.post('/api/crop', async (req, res) => {
  const { file, output: requested, x, y, width, height } = req.body || {};
  try {
    const { output, outPath } = await prepareOutput(file, requested);
    const values = [x, y, width, height];
    if (!values.every((v) => Number.isInteger(v) && v >= 0 && v % 2 === 0) || width === 0 || height === 0) {
      throw new HttpError(400, 'Crop values must be even, non-negative integers');
    }
    const info = await probe(file);
    if (x + width > info.width || y + height > info.height) {
      throw new HttpError(400, `Crop exceeds the video size (${info.width}x${info.height})`);
    }
    await runFfmpeg([
      '-i', path.join(FOOTAGE_DIR, file),
      '-vf', `crop=${width}:${height}:${x}:${y}`,
      '-c:v', 'libx264', '-crf', '18', '-preset', 'fast', '-pix_fmt', 'yuv420p',
      '-c:a', 'copy',
      '-movflags', '+faststart',
      outPath,
    ]);
    res.json({ ok: true, output, message: `Saved ${output} (${width}x${height})` });
  } catch (err) {
    sendError(res, err);
  }
});

// Perspective-correct the polygon `points` ([x, y] for top-left, top-right,
// bottom-left, bottom-right, in source pixels) into an upright rectangle.
app.post('/api/skew', async (req, res) => {
  const { file, output: requested, points } = req.body || {};
  try {
    const { output, outPath } = await prepareOutput(file, requested);
    const info = await probe(file);
    const valid = Array.isArray(points) && points.length === 4 && points.every((p) =>
      Array.isArray(p) && p.length === 2 && p.every(Number.isFinite) &&
      p[0] >= 0 && p[0] <= info.width && p[1] >= 0 && p[1] <= info.height);
    if (!valid) {
      throw new HttpError(400, `Points must be 4 [x, y] pairs within ${info.width}x${info.height}`);
    }
    if (!SkewGeometry.isConvex(points)) {
      throw new HttpError(400, 'The polygon must be convex (edges must not cross)');
    }
    const { width, height } = SkewGeometry.outputSize(points, info.width, info.height);
    // perspective maps the four points to the frame corners (at the input
    // size); scale then sets the undistorted output size
    const coords = points.flat().map((v) => v.toFixed(2)).join(':');
    await runFfmpeg([
      '-i', path.join(FOOTAGE_DIR, file),
      '-vf', `perspective=${coords}:interpolation=cubic,scale=${width}:${height}:flags=lanczos,setsar=1`,
      '-c:v', 'libx264', '-crf', '18', '-preset', 'fast', '-pix_fmt', 'yuv420p',
      '-c:a', 'copy',
      '-movflags', '+faststart',
      outPath,
    ]);
    res.json({ ok: true, output, message: `Saved ${output} (${width}x${height})` });
  } catch (err) {
    sendError(res, err);
  }
});

// Export final/<name> as <base>.mjpeg to every screen size that doesn't have
// it yet, stretching to fill each screen exactly. The size's longer side is
// laid along the source's longer side, so a landscape source with a 368x448
// size is exported at 448x368 (still into the 368-448 folder). Returns the
// sizes made, as "WxH".
async function buildScreenSizes(name) {
  const made = [];
  const source = await probe(name, 'final');
  const sourceLandscape = source.width > source.height;
  for (const size of await loadScreenOutputs()) {
    const dir = sizeDir(size);
    const outPath = path.join(dir, espName(name));
    if (await exists(outPath)) continue;
    let { width, height } = size;
    if (source.width !== source.height && width !== height && sourceLandscape !== width > height) {
      [width, height] = [height, width];
    }
    await fs.mkdir(dir, { recursive: true });
    await runFfmpeg([
      '-i', path.join(FINAL_DIR, name),
      '-vf', `fps=${ESP_FPS},scale=${width}:${height}:flags=lanczos,setsar=1`,
      '-an',
      '-c:v', 'mjpeg', '-q:v', '3', '-pix_fmt', 'yuvj420p',
      '-f', 'mjpeg',
      outPath,
    ]);
    made.push(`${width}x${height}`);
  }
  return made;
}

// Copy a raw clip into final/ (optionally under a new name), then export it at
// every screen size.
app.post('/api/final', async (req, res) => {
  const { file, name } = req.body || {};
  try {
    const { output, outPath } = await prepareOutput(file, name || file, FINAL_DIR);
    if (!(await exists(path.join(FOOTAGE_DIR, file)))) {
      throw new HttpError(404, `No such raw file: ${file}`);
    }
    await fs.mkdir(FINAL_DIR, { recursive: true });
    await fs.copyFile(path.join(FOOTAGE_DIR, file), outPath, fsConstants.COPYFILE_EXCL);
    const made = await buildScreenSizes(output);
    res.json({ ok: true, output, message: `Copied to final/${output}; made ${made.join(', ') || 'no new sizes'}` });
  } catch (err) {
    sendError(res, err);
  }
});

// Make any screen sizes a final clip is missing (e.g. after adding a size to
// screen-outputs.json)
app.post('/api/final/sizes', async (req, res) => {
  const { name } = req.body || {};
  try {
    if (!isValidFile(name) || !(await exists(path.join(FINAL_DIR, name)))) {
      throw new HttpError(404, 'No such final clip');
    }
    const made = await buildScreenSizes(name);
    res.json({ ok: true, message: `${name}: made ${made.join(', ') || 'nothing (all sizes exist)'}` });
  } catch (err) {
    sendError(res, err);
  }
});

app.listen(PORT, () => {
  console.log(`Listening on http://localhost:${PORT} (footage: ${FOOTAGE_DIR})`);
});
