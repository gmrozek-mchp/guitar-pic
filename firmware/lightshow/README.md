# lightshow

Lighting node for the marvin T1S bus — drives LEDs / lamps in time to the music for a stage
light-show effect. A PIC32CM6408PL10048 T1S PLCA follower (node id 7, `node_type = 5`). Primary
purpose: run light patterns in time to the music from a future beat/music signal source.

- **What lightshow is** (purpose, hardware, T1S link, firmware design, milestones): [`SPEC.md`](SPEC.md)
- **Running diary** (decisions, open questions, progress): [`docs/journal.md`](docs/journal.md)

## Structure

| Path                     | Purpose                                                                          |
|--------------------------|----------------------------------------------------------------------------------|
| SPEC.md                  | Specification — what lightshow is                                                 |
| docs/journal.md          | Working journal — planning and progress                                          |
| config.mcc               | MPLAB Code Configurator project (device config + generated peripheral libraries) |
| .vscode/settings.json    | Workspace-specific settings                                                      |
| .vscode/lightshow.mplab.json | The MPLAB project file, should not be deleted                                |
