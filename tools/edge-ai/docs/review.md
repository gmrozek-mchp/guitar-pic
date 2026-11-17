# review

This is the document to push back on. Three categories: things we know can fail (risks), forks we haven't decided (open questions), and shapes we explicitly rejected (alternatives).

> Resolved risks are marked inline. The remaining live risk is inheriting marvin's bugs.

## Risks

- **Strum class imbalance (managed).** The collapsed strum bit is sparse (~7–13% on hard). Weighted BCE (`--strum-weight`) + a strum threshold + the monostable post-processor keep it off the "always 0" minimum; the *binding* constraint turned out to be model **capacity** on this class, fixed by deploying 16 channels (see [model.md §8](model.md)).
- **~~Window-too-short failure mode~~ (resolved → receptive field).** Reframed: the limiter wasn't the *window* but the *receptive field*. RF 21 `(1,4)` couldn't reach the ~48-sample lag (frets capped ~0.86); RF 85 `(1,4,16)` covers it (frets ~0.99). Window only needs to be ≥ RF. See [model.md §4](model.md).
- **Per-bit independence vs. chord structure (not hit).** Treating bits as independent sigmoids ignores that real chords occupy a tiny subset of the 2⁶ space. Hasn't capped accuracy so far; if it does, switch to a multi-class head over common chord shapes — see [§alternatives](#alternatives-considered).
- **Multi-difficulty is ill-posed, not just out-of-distribution (resolved → one model per difficulty).** Scroll speed varies with difficulty (measured 2026-08-08: ~420 ms observation lead on easy vs ~250 ms on hard), so the same photo-dip pattern maps to different correct delays per tier. With 5 sensors at one row and no difficulty input, mixing tiers is an ill-posed regression more data can't fix. Resolution: one model per difficulty, selected by marvin at run start ([runtime.md §6](runtime.md)); a scroll-speed sensor ([Q5](#q5-photoxistor-placement)) is the alternative.
- **Inheriting marvin's bugs.** Distillation can't beat the teacher. Mistimed strums in marvin become mistimed strums in the AI. Acceptable for v1; future work could layer human-play recordings on top (see [Q4](#q4-strike-line-cv-detector-as-alternative-label-source)).
- **~~Cross-stream label skew~~ (resolved → `commanded-fb`).** The legacy `actuator` export paired a send-path label with a receive-path feature across two USB directions. The fretboard now latches marvin's command into the same frame as the ADC scan, so the pairing is atomic ([training.md §1](training.md)).

## Open questions

<a id="q1-hold-vs-edge-channel-per-fret"></a>
### Q1: Hold vs. edge channel per fret — add HW now, or wait?

The current 5-channel ADC at 240 Hz may alias on fast transients. Two ways to add information:

- **(a)** Two photoxistors per fret at *different* screen rows — gives the model an intrinsic "note approaching" + "note here" pair, doubling temporal resolution at hardware cost.
- **(b)** One extra photoxistor per fret with a colour filter, mimicking `cv_marvin_v1`'s colour-edge channel — separates "any brightness here" from "specifically a green note here".

Either doubles the ADC scan time. Greg wants to pursue (a) as a hardware revision; scope is in the journal open questions.

<a id="q2-ar-feedback"></a>
### Q2: AR feedback — let the model see its own past output?

Most fret-bit predictions at time `T` are highly correlated with the prediction at `T-1` (held notes). An AR model could be much smaller / more accurate. Cost: a feedback loop at runtime (bugs amplify), and harder training (teacher forcing vs. scheduled sampling). Defer until non-AR baseline is known.

<a id="q3-look-ahead-vs-now-cast"></a>
### Q3: Window length — measure first, then sweep narrowly

Settled for now: the window must be ≥ the receptive field, and the RF must cover the photo→strum lag, which is measurable per difficulty with `edge-ai lag` ([model.md §4](model.md)). Deploy is RF 85 / window 85.

**Look-ahead vs. now-cast.** Currently formulated as "predict the command at *now*". Could instead predict at `T + K_ms` to cover model+actuator latency. The T1S hop adds ~1.5–2 ms (journal 2026-06-17); `--label-lead` exists for this but is unused. Revisit if play is late, or when mechanical actuators land (system [SPEC.md §5](../../../SPEC.md), [hardware/actuators/](../../../hardware/actuators/)).

<a id="q4-strike-line-cv-detector-as-alternative-label-source"></a>
### Q4: Strike-line CV detector as alternative label source?

If distillation hits a noise ceiling, an alternative is to add a `cv_strike_v1` detector on marvin that samples the *strike row* directly at 60 Hz. Cleaner labels for fret state, but loses strum-event timing — would need to re-add a rule-based "strum when strike row goes active". Flag for now, don't build.

<a id="q5-photoxistor-placement"></a>
### Q5: Phototransistor placement / count — what's optimal?

Today's PCB ([hardware/Sensor-LCD5/](../../../hardware/Sensor-LCD5/)) has 5 sensors above the strike line. Optimum-for-this-AI is unknown:

- *Higher* on the screen → longer lookahead but more time for the note appearance to change.
- *At the strike line* → simplest label (the photo dip *is* the answer) but no anticipation, breaking strum scheduling.
- *Two rows* → see [Q1](#q1-hold-vs-edge-channel-per-fret).
- *A dedicated fret-line / scroll-speed channel* → a 6th sensor aimed at a column where only the highway's horizontal bar/measure lines pass (not the colored notes). The frequency at which lines cross it **is** the scroll speed — the exact variable single-row note sensors can't observe (see the multi-difficulty risk above). Could let one model span difficulties instead of one-per-difficulty. Open whether a tiny int8 model can actually use it to modulate strum timing. v1 stays 5-channel single-difficulty; flagged for future work.



## Alternatives considered

Three other shapes were on the table before the current distillation choice landed:

- **Shape A — frame classifier with co-located CV.** Model predicts `pressed_mask` from a single ADC sample; labels come from a *new* CV detector at the photoxistor row. Pros: small model, easy to label. Rejected because it still leaves the strum decision outside the AI, and forces mechanical alignment between CV and photoxistors — exactly the problem we're trying to dissolve.
- **Shape B — sequence-to-strike-line oracle.** Model takes a causal ADC window, outputs what's at the strike row right now. Labels come from a new strike-row CV detector. Pros: clean physical meaning ("predict the moment of truth"). Rejected because, like A, it doesn't include strum timing — a downstream rule still has to schedule strums. We'd be solving the easier half of the problem twice.
- **Shape D — two-head (continuous fret + sparse strum events).** One head for held-note state, one head for `[time-to-strum, mask, dir]` events. Closer to the underlying physics. Rejected as v1 because it's significantly more complex to architect, train, and validate. Worth revisiting if per-bit independence ever caps strum timing — D's sparse-event head is a natural cure.
