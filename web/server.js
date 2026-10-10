const express = require('express');
const fs = require('fs/promises');
const path = require('path');
const { execFile } = require('child_process');
const SkewGeometry = require('./public/skew-geometry');

const PORT = process.env.PORT || 3000;
const FOOTAGE_DIR = process.env.FOOTAGE_DIR || '/footage';

const app = express();
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// Raw footage files (supports range requests for video seeking)
app.use('/video', express.static(FOOTAGE_DIR));

function isValidFile(file) {
  return typeof file === 'string' && file !== '' && path.basename(file) === file;
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

function probe(file) {
  const args = [
    '-v', 'error',
    '-select_streams', 'v:0',
    '-show_entries', 'stream=width,height,r_frame_rate,avg_frame_rate,nb_frames:format=duration',
    '-of', 'json',
    path.join(FOOTAGE_DIR, file),
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

app.get('/api/files', async (req, res) => {
  try {
    const entries = await fs.readdir(FOOTAGE_DIR, { withFileTypes: true });
    const names = entries
      .filter((e) => e.isFile() && !e.name.startsWith('.'))
      .map((e) => e.name);
    // Most recently modified first
    const withTimes = await Promise.all(
      names.map(async (name) => {
        const { mtimeMs } = await fs.stat(path.join(FOOTAGE_DIR, name));
        return { name, mtimeMs };
      })
    );
    const files = withTimes
      .sort((a, b) => b.mtimeMs - a.mtimeMs)
      .map((f) => f.name);
    res.json({ dir: FOOTAGE_DIR, files });
  } catch (err) {
    res.status(500).json({ error: `Cannot read ${FOOTAGE_DIR}: ${err.message}` });
  }
});

app.get('/api/probe', (req, res) => {
  const { file } = req.query;
  if (!isValidFile(file)) {
    return res.status(400).json({ error: 'Invalid file' });
  }
  probe(file)
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
// extension if none was given), refusing to overwrite an existing file.
async function prepareOutput(file, output) {
  if (!isValidFile(file)) throw new HttpError(400, 'Invalid file');
  output = typeof output === 'string' ? output.trim() : '';
  if (!isValidFile(output) || output.startsWith('.')) {
    throw new HttpError(400, 'Invalid new filename');
  }
  if (!path.extname(output)) output += path.extname(file) || '.mp4';
  const outPath = path.join(FOOTAGE_DIR, output);
  try {
    await fs.access(outPath);
  } catch {
    return { output, outPath };
  }
  throw new HttpError(409, `${output} already exists`);
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

app.listen(PORT, () => {
  console.log(`Listening on http://localhost:${PORT} (footage: ${FOOTAGE_DIR})`);
});
