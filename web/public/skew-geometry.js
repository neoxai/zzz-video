// Geometry for turning a 4-point polygon back into a rectangle. Shared by the
// browser (skew.html) and the server, so both agree on the output size.
// Points are [x, y] in source pixels, ordered top-left, top-right,
// bottom-left, bottom-right (the order ffmpeg's perspective filter uses).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.SkewGeometry = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  const dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];

  // Angle between segment a1->a2 and segment b1->b2, in radians (0 to π/2)
  function edgeAngle(a1, a2, b1, b2) {
    const da = Math.atan2(a2[1] - a1[1], a2[0] - a1[0]);
    const db = Math.atan2(b2[1] - b1[1], b2[0] - b1[0]);
    const diff = Math.abs(da - db) % Math.PI;
    return Math.min(diff, Math.PI - diff);
  }

  // True when the polygon tl -> tr -> br -> bl is convex and not self-intersecting
  function isConvex(points) {
    const [tl, tr, bl, br] = points;
    const ring = [tl, tr, br, bl];
    let sign = 0;
    for (let i = 0; i < 4; i++) {
      const a = ring[i];
      const b = ring[(i + 1) % 4];
      const c = ring[(i + 2) % 4];
      const z = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);
      if (Math.abs(z) < 1e-9) return false;
      if (sign && Math.sign(z) !== sign) return false;
      sign = Math.sign(z);
    }
    return true;
  }

  // Width / height of the real rectangle that the polygon is a perspective
  // view of, using Zhang & He, "Whiteboard scanning and image enhancement"
  // (2007), with the principal point assumed at the frame centre. The camera's
  // focal length is recovered from the two vanishing points; when only one
  // pair of edges converges it can't be, so a typical 60° horizontal field of
  // view is assumed instead.
  function aspectRatio(points, frameWidth, frameHeight) {
    const [tl, tr, bl, br] = points;
    const edgeRatio = (dist(tl, tr) + dist(bl, br)) / (dist(tl, bl) + dist(tr, br));

    const u0 = frameWidth / 2;
    const v0 = frameHeight / 2;
    const m = [tl, tr, bl, br].map(([x, y]) => [x - u0, y - v0, 1]);
    const [m1, m2, m3, m4] = m;
    const k2 = dot(cross(m1, m4), m3) / dot(cross(m2, m4), m3);
    const k3 = dot(cross(m1, m4), m2) / dot(cross(m3, m4), m2);
    const n2 = m2.map((v, i) => k2 * v - m1[i]);
    const n3 = m3.map((v, i) => k3 * v - m1[i]);
    if (![k2, k3, ...n2, ...n3].every(Number.isFinite)) return edgeRatio;

    const scaleRef = Math.max(frameWidth, frameHeight);
    const defaultF = frameWidth / 2 / Math.tan(Math.PI / 6);
    let f2 = -(n2[0] * n3[0] + n2[1] * n3[1]) / (n2[2] * n3[2]);
    // With a pair of opposite edges within ~0.5° of parallel, their vanishing
    // point (and so f2) is just noise. That, or an imaginary / absurd focal
    // length, means falling back to the assumed camera.
    const nearlyParallel = Math.min(edgeAngle(tl, tr, bl, br), edgeAngle(tl, bl, tr, br)) < (0.5 * Math.PI) / 180;
    if (nearlyParallel || !(f2 > 0) || Math.sqrt(f2) < scaleRef * 0.05 || Math.sqrt(f2) > scaleRef * 40) {
      f2 = defaultF * defaultF;
    }
    const weighted = (n) => (n[0] * n[0] + n[1] * n[1]) / f2 + n[2] * n[2];
    const r = Math.sqrt(weighted(n2) / weighted(n3));
    return Number.isFinite(r) && r > 0 ? r : edgeRatio;
  }

  // Output size: height is the average of the left and right edges, width
  // follows from the recovered aspect ratio. Both rounded to even numbers.
  function outputSize(points, frameWidth, frameHeight) {
    const [tl, tr, bl, br] = points;
    const ratio = aspectRatio(points, frameWidth, frameHeight);
    const height = (dist(tl, bl) + dist(tr, br)) / 2;
    const even = (n) => Math.max(2, Math.round(n / 2) * 2);
    return { width: even(height * ratio), height: even(height), ratio };
  }

  return { isConvex, aspectRatio, outputSize };
});
