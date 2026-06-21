# Top-level vitals container.
# Payloads are incremental during capture: any field may be empty while
# processing continues. Rate metrics (breathing.rate, cardio.pulse_rate,
# cardio.hrv) update per event/window; frame-driven metrics (face.*) update
# near frame cadence, rate-limited to about 30 Hz.
Breathing breathing
Eda eda
Face face
Cardio cardio
