# lemmy

Animation node for the marvin T1S bus — an animated guitar-playing puppet driven by two R/C hobby
servos (neck joint for head-nod / head-bang, bottom jaw for mouth). A PIC32CM6408PL10048 T1S PLCA
follower (node id 6). Primary purpose: nod the head in time to the music from a future beatbox node.

- **What lemmy is** (purpose, hardware, T1S link, firmware design, milestones): [`SPEC.md`](SPEC.md)
- **Running diary** (decisions, open questions, progress): [`docs/journal.md`](docs/journal.md)

## Structure

| Path                     | Purpose                                                                          |
|--------------------------|----------------------------------------------------------------------------------|
| SPEC.md                  | Specification — what lemmy is                                                     |
| docs/journal.md          | Working journal — planning and progress                                          |
| config.mcc               | MPLAB Code Configurator project (device config + generated peripheral libraries) |
| .vscode/settings.json    | Workspace-specific settings                                                      |
| .vscode/lemmy.mplab.json | The MPLAB project file, should not be deleted                                    |
