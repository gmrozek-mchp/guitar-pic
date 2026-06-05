# review

This is the document to push back on. Three categories: things we know can fail (risks), forks we haven't decided (open questions), and shapes we explicitly rejected (alternatives).

> Several phase-2 risks below are now **resolved** by the as-built model — marked inline. The remaining live risks are inherited-teacher bugs and the cross-stream label skew (the latter mostly closed by the atomic `actuator-fb` schema).

## Risks

- **Strum class imbalance (managed).** The collapsed strum bit is sparse (~7–13% on hard). Weighted BCE (`--strum-weight`) + a strum threshold + the monostable post-processor keep it off the "always 0" minimum; the *binding* constraint turned out to be model **capacity** on this class, fixed by deploying 16 channels (see [model.md §8](model.md)).
- **~~Window-too-short failure mode~~ (resolved → receptive field).** Reframed: the limiter wasn't the *window* but the *receptive field*. RF 21 `(1,4)` couldn't reach the ~48-sample lag (frets capped ~0.86); RF 85 `(1,4,16)` covers it (frets ~0.99). Window only needs to be ≥ RF. See [model.md §4](model.md).
- **Per-bit independence vs. chord structure (not hit).** Treating bits as independent sigmoids ignores that real chords occupy a tiny subset of the 2⁶ space. Hasn't capped accuracy so far; if it does, switch to a multi-class head over common chord shapes — see [§alternatives](#12-alternatives-considered).
- **Multi-difficulty is ill-posed, not just out-of-distribution (resolved → single-difficulty v1).** Original guidance here was "train on Easy + Medium + Expert." That is wrong if scroll speed varies with difficulty: the photo→strum lag = `(pixel gap photo-row→CV-row)/scroll_speed + 220 ms`, so the same photo-dip pattern maps to *different* correct delays across difficulties. With 5 sensors at one row and no difficulty input, the model can't disambiguate — mixing difficulties is an ill-posed regression more data can't fix. **Resolution:** v1 trains on a single difficulty (**hard** for v1; see journal decision log + [training.md §4](training.md)). Per-difficulty models, or a fret-line scroll-speed sensor ([Q5](#q5-photoxistor-placement)), are the multi-difficulty routes for later.
- **Inheriting marvin's bugs.** Distillation can't beat the teacher. Mistimed strums in marvin become mistimed strums in the AI. Acceptable for v1; future work could layer human-play recordings on top (see [Q4](#q4-strike-line-cv-detector-as-alternative-label-source)).
- **CDC + RTOS jitter in labels, and a cross-stream pairing skew.** `PERF_REC_ACTUATOR` timestamps the moment marvin *emits* the command, not the moment fretboard asserts the GPIO. Sharper still: the label is stamped on the **send** path and the feature (`FRETBOARD_RAW`) on the **receive** path — opposite USB-CDC directions with independent latency/jitter — so the exporter's forward-fill pairs them with an unknown, varying skew. Probably fine for v1 (fretboard saw that same jitter at runtime), but it's a candidate accuracy ceiling. The clean fix is to pair sensor + applied-actuator-state atomically on the fretboard and carry both on the wire (fretboard journal open-questions #3/#4; edge-ai journal). Measure the impact in phase 2 before committing to the firmware change.

## Open questions

> Reviewers: each of these is a real fork. Speak up *before* phase 1 lands.

<a id="q1-hold-vs-edge-channel-per-fret"></a>
### Q1: Hold vs. edge channel per fret — add HW now, or wait?

The current 5-channel ADC at 240 Hz may alias on fast transients. Two ways to add information:

- **(a)** Two photoxistors per fret at *different* screen rows — gives the model an intrinsic "note approaching" + "note here" pair, doubling temporal resolution at hardware cost.
- **(b)** One extra photoxistor per fret with a colour filter, mimicking `cv_marvin_v1`'s colour-edge channel — separates "any brightness here" from "specifically a green note here".

Either doubles the ADC scan time. Bias is to start with the existing 5 channels and only add HW if phase 2 shows the model is starved for input.

<a id="q2-ar-feedback"></a>
### Q2: AR feedback — let the model see its own past output?

Most fret-bit predictions at time `T` are highly correlated with the prediction at `T-1` (held notes). An AR model could be much smaller / more accurate. Cost: a feedback loop at runtime (bugs amplify), and harder training (teacher forcing vs. scheduled sampling). Defer until non-AR baseline is known.

<a id="q3-look-ahead-vs-now-cast"></a>
### Q3: Window length — measure first, then sweep narrowly

Window length is the most important hyperparameter, but with difficulty fixed it need not be a blind sweep. The photo→strum lag is a constant we can **measure** by cross-correlating photo dips against strum events on real data (the `edge-ai lag` utility); size the causal window to span the measured lag + margin, then sweep ±1–2 samples around it. (`TP_STRUM_DELAY_MS = 220` is only the fixed part — the full lag includes scroll-time from the photo row to marvin's CV row.)

**Look-ahead vs. now-cast.** Currently formulated as "predict the command at *now*". Could instead predict at `T + K_ms` to cover model+actuator latency. Irrelevant while actuators are GPIOs (latency ≈ 0); revisit when mechanical actuators land (system [SPEC.md §5](../../../SPEC.md), [hardware/actuators/](../../../hardware/actuators/)).

<a id="q4-strike-line-cv-detector-as-alternative-label-source"></a>
### Q4: Strike-line CV detector as alternative label source?

If phase 2 reveals that distillation hits a noise ceiling (CDC jitter + chord-window quantisation), an alternative is to add a `cv_strike_v1` detector on marvin that samples the *strike row* directly at 60 Hz. Cleaner labels for fret state, but loses strum-event timing — would need to re-add a rule-based "strum when strike row goes active". Flag for now, don't build.

<a id="q5-photoxistor-placement"></a>
### Q5: Phototransistor placement / count — what's optimal?

Today's PCB ([hardware/Sensor-LCD5/](../../../hardware/Sensor-LCD5/)) has 5 sensors above the strike line. Optimum-for-this-AI is unknown:

- *Higher* on the screen → longer lookahead but more time for the note appearance to change.
- *At the strike line* → simplest label (the photo dip *is* the answer) but no anticipation, breaking strum scheduling.
- *Two rows* → see [Q1](#q1-hold-vs-edge-channel-per-fret).
- *A dedicated fret-line / scroll-speed channel* → a 6th sensor aimed at a column where only the highway's horizontal bar/measure lines pass (not the colored notes). The frequency at which lines cross it **is** the scroll speed — the exact variable single-row note sensors can't observe (see the multi-difficulty risk above). Could let one model span difficulties instead of one-per-difficulty. Open whether a tiny int8 model can actually use it to modulate strum timing. v1 stays 5-channel single-difficulty; flagged for future work.

Open until phase 2 data tells us which placement makes the model's job easiest.

## 12. Alternatives considered

Three other shapes were on the table before the current distillation choice landed:

- **Shape A — frame classifier with co-located CV.** Model predicts `pressed_mask` from a single ADC sample; labels come from a *new* CV detector at the photoxistor row. Pros: small model, easy to label. Rejected because it still leaves the strum decision outside the AI, and forces mechanical alignment between CV and photoxistors — exactly the problem we're trying to dissolve.
- **Shape B — sequence-to-strike-line oracle.** Model takes a causal ADC window, outputs what's at the strike row right now. Labels come from a new strike-row CV detector. Pros: clean physical meaning ("predict the moment of truth"). Rejected because, like A, it doesn't include strum timing — a downstream rule still has to schedule strums. We'd be solving the easier half of the problem twice.
- **Shape D — two-head (continuous fret + sparse strum events).** One head for held-note state, one head for `[time-to-strum, mask, dir]` events. Closer to the underlying physics. Rejected as v1 because it's significantly more complex to architect, train, and validate. **Worth revisiting if the per-bit-independence loss in the current shape produces poor strum timing in phase 2** — D's sparse-event head is a natural cure.
