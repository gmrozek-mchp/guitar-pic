#ifndef FRETBOARD_CONFIG_H
#define FRETBOARD_CONFIG_H

/*
 * Build-time configuration.
 *
 * The fretboard is a 10BASE-T1S node (PLCA follower id 4) that both senses and
 * drives. Each 240 Hz tick it scans the five phototransistors and runs an
 * on-device int8 model that infers the Wii-guitar button bitmask from the ADC
 * window; over T1S it then:
 *   - streams the 17-byte data frame (ADC scan + inferred bitmask) to the marvin
 *     coordinator (logging / edge-ai training), and
 *   - sends the inferred 1-byte command directly to the guitar node (actuation,
 *     peer-to-peer — marvin coordinates/logs but is out of the command path).
 * It has no local Wii-guitar outputs. See t1s_detector.{c,h} and
 * docs/t1s-podl-link.md.
 *
 * Inference implementation:
 *   0 = recompute (model_infer.c)         — re-runs the receptive field each tick.
 *   1 = streaming  (model_infer_stream.c) — caches per-layer columns, locks to the
 *       240 Hz sample rate; requires a model trained at window >= the receptive
 *       field (85). (default)
 *
 * Both model_infer.c and model_infer_stream.c stay in the project: each guards its
 * body on MODEL_INFER_STREAMING, so only the active one compiles its static
 * buffers (the inactive TU is empty). Flip here or with -D.
 */
#ifndef MODEL_INFER_STREAMING
#define MODEL_INFER_STREAMING 1
#endif

#endif /* FRETBOARD_CONFIG_H */
