# media

Photos and video used by [SETUP.md](../SETUP.md) and [DEMO.md](../DEMO.md).

- Photos are 1600 px on the long edge.
- Videos are H.264 at about 540p with mono AAC audio.
- Both are stripped of metadata, including location.

To add more, re-encode before committing:

```sh
ffmpeg -i in.mov -map_metadata -1 -c:v libx264 -preset slow -crf 30 -r 30 -pix_fmt yuv420p \
       -c:a aac -b:a 64k -ac 1 -movflags +faststart video/out.mp4
ffmpeg -i in.jpg -map_metadata -1 -vf "scale='if(gt(iw,ih),1600,-2)':'if(gt(iw,ih),-2,1600)'" -q:v 5 photos/out.jpg
```

## video/

| File | Shows |
|---|---|
| `system-walkthrough.mp4` | every connection, end to end along the T1S chain |
| `power-up.mp4` | power-up order, connecting player 1 / player 2, launching GH3, speaker remote |
| `puppet-setup.mp4` | tripod, bracket, puppet, guitar and servo leads |
| `showdown.mp4` | SHOWDOWN: visitor vs. the machine |
| `single-player-demo.mp4` | 1-player hard; computer vision vs. NEURAL NETWORK control |
| `easier-mode-song.mp4` | casual 2-player at an easier difficulty |
| `stopping-a-song.mp4` | STOP vs. quitting a song from the Wiimotes screen |
| `captouch-guitar-1.mp4` | swapping to the capacitive-touch guitar |
| `captouch-guitar-2.mp4`, `captouch-guitar-3.mp4` | playing with the capacitive-touch guitar |

## photos/

Named for what they show: `booth-*`, `marvin-*`, `wii-*`, `remote-node`, `hdmi-*`,
`fretboard-node`, `beatbox-node`, `lightshow-controller`, `speaker-*`, `lemmy-*`,
`player-guitar*`, `captouch-*`.
