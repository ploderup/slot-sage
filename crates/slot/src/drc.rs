// How far the resampler may stretch or squeeze the sound to keep the ring near
// its target. On the Mini Plus the ring sat at 900-1,900 of its 3,200-frame
// target with 0.5% (a tester's log), so the device drains faster than the
// nominal rate by more than that; 2% lets it settle nearer the target. It is
// only reached when the ring is far off, so the pitch change stays small.
const MAX_CORRECTION: f64 = if cfg!(feature = "miyoomini") {
    0.02
} else {
    0.005
};

pub fn drc_ratio(queued_frames: usize, target_frames: usize) -> f64 {
    if target_frames == 0 {
        return 1.0;
    }
    let error = target_frames as f64 - queued_frames as f64;
    let ratio = 1.0 + MAX_CORRECTION * error / target_frames as f64;
    ratio.clamp(1.0 - MAX_CORRECTION, 1.0 + MAX_CORRECTION)
}

pub fn drc_target(capacity_frames: usize) -> usize {
    capacity_frames / 2
}
