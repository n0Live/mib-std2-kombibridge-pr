# native/ — DisplayManager hook for the Android Auto cluster map

`libkb_dm.so` is loaded into DisplayManager (`tsd.mibstd2.system.displaymanager`) through
`LD_PRELOAD` in `/tsd/etc/system/cpu.conf`. While the patched libgal (built with `Config.ClusterMap.ENABLED = true`)
streams Android Auto's cluster video into `/dev/shmem/aatokombi-cluster-video`
(`shim/src/cluster_video_shm.h`), DisplayManager's cluster reader gets the phone's H.264 access
units instead of its own encoder's. TS packing, the ring to the J5, MOST and the cluster's decoder
stay stock. When the phone stops, the cluster is back on the stock map from the stock encoder's
next key frame.

## Credits

The DisplayManager seam (the encoder's `mmfbq_*` buffer queue in `libOpenMAXAL.so.1`, the
`MostSink` → `RingIO::writeAquire` call site, the cluster decoder's limits) was found and published
by Anton Perk in [mst2-cluster-nav](https://github.com/perkthefox/mst2-cluster-nav)
(CC BY-NC-SA 4.0), where it carries CarPlay's cluster map. This directory is an independent
implementation for Android Auto; the firmware facts were re-checked on the `MST2_EU_SK_ZR_P0480T`
dump (`libOpenMAXAL.so.1` sha256 `c31fe983…`, displaymanager `932efd26…`). Any other build: the
hook passes everything through and logs it.

## Comparison with mst2-cluster-nav, and what was taken from it

| | mst2-cluster-nav (CarPlay) | this repository (Android Auto) |
|---|---|---|
| Source of the stream | SAL hook: second AirPlay screen, own TCP receiver, AES-CTR | patched libgal: second AAP video sink (cluster display) |
| Geometry | phone encodes 1010×376 (1024×384 coded), same as the stock stream | phone encodes 1280×720 with 244 lines of margin; SPS cropped to 1280×476 |
| Bitrate cap | socket read throttled to 10 Mbit/s, 250 ms burst | **taken over**: 10 Mbit/s, 250 ms burst, enforced by delaying AAP ACKs (`CLUSTER_KBPS`) |
| Key frames | `forceKeyFrame` command | none in AAP: video focus NATIVE→PROJECTED (`CLUSTER_IDR_MODE`) |
| SAL → DM hand-off | shm, 24 × 256 000 B slots, seq-lock, semaphore, `alive_ms`, `want_idr` | **same design**, own layout (`shim/src/cluster_video_shm.h`) |
| DM seam | `mmfbq_get_and_wait` return-address check, queue found by SPS, stock frames returned, start at IDR, back to stock at its next IDR | **same**, independently written |
| MOST ring | wait up to 300 ms in `RingIO::writeAquire` from MostSink, 10 s stats line | **taken over** |
| Stock cluster map | hidden while the phone video runs (`setMapInvisible` event, resent every 2 s) | **taken over** in the jar (`de.aatokombi.KombiMap`, `Config.HIDE_STOCK_KOMBI_MAP_DURING_AA_VIDEO`); state from libgal via `/dev/shmem/aa_cluster_video` |
| Safety | kill switch, trial boot, `runHMI.sh` guard, crash guard, atomic `cpu.conf` edits with backups | kill switch, crash guard, copy-and-compare before the `cpu.conf` edit, stock copy kept; **no trial boot / `runHMI.sh` guard yet** |

What reaches MOST: while the phone owns the cluster, the stock encoder's frames are handed back
to it and never reach the TS packer, so the MOST channel carries only the phone's stream. Hiding
the stock map stops the navigation from drawing it and the encoder from encoding it (CPU); the
bitrate cap is what keeps the MOST channel from overflowing.

## Build

`./build.sh MIBHMI.jxe libext.google.gal.receiver.so dist-cluster` builds it together with the jar
and libgal (Docker) when `Config.ClusterMap.ENABLED` is true (or `CLUSTER_SINK=1` is set).
Stand-alone: `sh native/build.sh` (clang + ld.lld, no QNX SDK).

## Install / remove

With the toolbox: put `libkb_dm.so` next to the patched libgal (SD `custom/sal/`) and run
**Enable AAtoKombi**; **Disable AAtoKombi** removes it. The toolbox runs the same checks as the
telnet installer below (including the self check) and installs the rest without the hook when the
check fails. The toolbox's **Dump files** item also copies `cpu.conf`, the DisplayManager binary,
`libOpenMAXAL.so.1`, `libtsd.common.so` and `libmmrndclient.so.1` - the files this hook is tied to
(see the next section).

By hand over telnet (the build no longer puts these scripts into `dist/`; take them from
`native/install/`; `FORCE=1` is only available here):

```sh
# FTP libkb_dm.so and install_dm_hook.sh / uninstall_dm_hook.sh to /tmp first
sh /tmp/install_dm_hook.sh /tmp/libkb_dm.so      # then reboot
sh /tmp/uninstall_dm_hook.sh                     # then reboot
```

Never install together with mst2-cluster-nav: both hook the same DisplayManager functions.

**Risk:** if the library named in `LD_PRELOAD` cannot be loaded, DisplayManager does not start and
the unit resets. The installer copies and compares the file before it edits `cpu.conf`; the stock
`cpu.conf` is kept in `/tsd/var/aatokombi/cpu.conf.stock`.

## Which firmware builds the hook supports, and how you find out

The hook is tied to one build of two files: the buffer-queue functions of `libOpenMAXAL.so.1` with
the reader's call site, and - for the MOST ring wait of the pass-through mode - one call site in the
DisplayManager binary. Their addresses are in `include/dm_offsets.h`, which is firmware 0480. For
another firmware the build makes that header itself from a **dump of the two files**
(`/usr/lib/libOpenMAXAL.so.1` and `/tsd/bin/displaymanager/tsd.mibstd2.system.displaymanager`, in one
folder, found below it by name):

    ./build.sh MIBHMI.jxe libext.google.gal.receiver.so dist DM_DUMP=/path/to/dump   # Linux / macOS
    DM_DUMP=/path/to/dump ./build.sh MIBHMI.jxe libext.google.gal.receiver.so        # the same, as a variable
    .\build.bat MIBHMI.jxe libext.google.gal.receiver.so dist DM_DUMP=C:\path\to\dump   # Windows
    DM_DUMP=/path/to/dump ./native/build.sh                                          # the library alone

`DM_DUMP=<folder>` may stand anywhere among the arguments. `build.bat` without it asks for the folder
in a dialog (Cancel builds for firmware 0480; `DM_DUMP=-` skips the question).

`tools/dm_offsets.py` finds the places by structure (the exported `mmfbq_*` symbols, the call inside
`BufferQueueGetAndWait`, the PLT stub and the one caller of `RingIO::writeAquire`), also writes the
instruction words found there, and the build output names the two files the library was built for;
the hook logs the same at start (`loaded, offsets for libOpenMAXAL ..., displaymanager ...`). It
fails with a message when it cannot find a place unambiguously. What it cannot read from the files -
the layout of the queue and its elements - is still assumed; a changed layout shows as a wrong
picture on the unit, not as an error. Tested against the 0480 dump (the result is equal to the
default header) and on made-up ELF files (`tests/dmoffsets`). The hook never assumes: inside DisplayManager it
compares them with what it knows and, on any difference, replaces nothing (log line
`libOpenMAXAL build differs: cluster video stays stock`). An unsupported firmware therefore gets
the stock cluster map, not a crash.

`install_dm_hook.sh` asks the same question **before** it edits `cpu.conf`: it loads the copied
library into a harmless process (`LD_PRELOAD=... KB_DM_CHECK=1 ls`) and reads its answer from
`/dev/shmem/aatokombi-dm.check`:

    loaded=1 omx=ok most=ok mmr=ok ring=ok

| field | ok means | otherwise |
|---|---|---|
| (the file exists) | the library can be loaded on this unit | **no install**: as a preload it would keep DisplayManager from starting |
| `omx` | libOpenMAXAL is the known build | `differs`: **no install** (the hook would be inert). `unknown`: could not be looked at from that process; decided again inside DisplayManager |
| `most` | MostSink call site as known | pass-through mode runs without the ring wait |
| `mmr` | mm-renderer client functions present | decode mode (the default) may not work |
| `ring` | `RingIO::writeAquire` found | pass-through ring wait unavailable |

`FORCE=1 sh install_dm_hook.sh ...` installs regardless. After the reboot the log must say
`libOpenMAXAL layout confirmed`. The check has been run on a host only (the ELF reader and the
installer's decisions); on a unit it is new - if it refuses a unit where the hook is known to work,
that is a bug in the check, and `FORCE=1` is the way round it.

## Per-app insets (navigation app detection)

Navigation apps draw their cards in different places, so one set of content insets does not suit
them all. With the table `/tsd/var/aatokombi/app-insets` on the unit, the hook finds out which app
draws the cluster map and libgal gives the phone that app's insets at once (0x8009, no reconnection).

The build's table is `jar/src/de/aatokombi/app-insets.txt` (edit it before the build): the jar
installs it when the unit has none, or still has an earlier build's default table (first line
`# AAtoKombi default table`; delete that line on the unit to keep your own edits). Format:

```
# app     [narrow|medium|wide] [guide] top bottom left right   (pixels of the phone's 1280x720 frame)
default   narrow        140 260 470 470
yandex    medium guide  140 150 400 400     # only during route guidance
google    150 260 470 470                   # no template: for all three
```

- The template (narrow / medium / wide) is chosen on the AA map with `Config.ClusterMap.INSETS_KEY`
  (zoom or OK) and kept in `/tsd/var/aatokombi/insets-template`. The most specific line wins
  (template, then `guide`); the app's lines win over the `default` lines, which are also used
  before the app is recognised. Guidance = the phone's navigation status ACTIVE or REROUTING.
- `Config.ClusterMap.HIDE_AA_GUIDANCE_CARD` (a build switch): during guidance the side insets are
  639 640, so the app draws no guidance card; top and bottom stay those of the table.
- Detection (`native/src/nav_app.c`, rules in `nav_app_classify.h`): while the table exists and
  the decode mode plays, the cluster display is read with QNX Screen 1 s after the video starts and
  then every 2 s; the colours of a 32x12 grid of the AA window tell the app (each app keeps its own
  palette on the cluster). An app seen twice in a row is written to `/dev/shmem/aa_nav_app`; the
  Android Auto splash and pictures that match no app change nothing. The last app is kept across
  reconnections, so its insets go out right at the next start. Log lines `nav: ...`.
- libgal (`shim/src/cluster_sink.h`, `shim/src/app_insets.h`) sends the matching line, else (if a
  line's insets were sent before) the insets of the connection. The table is read when the app,
  the template or the guidance state changes and at every start of the video.
- Finding the values: `echo "150 260 470 470" > /dev/shmem/aa_cluster_insets` sends insets at once
  and wins over the table while the file exists; remove it to return to the table.
- No table: no detection, nothing changes. A crash during a Screen call switches detection off
  until the next reboot.

## Off switches and diagnostics

- An SD card with an empty file `aatokombi-video-disable`, or `/tsd/var/aatokombi/video-off`:
  pass-through within 2 s.
- Crash guard, both modes: `/tsd/var/aatokombi/dm-busy` exists while the phone's video is on the
  cluster (written once per video session, removed when it ends normally). Found at start = the
  session before ended in a crash or power cut: the log says `the previous video session did not
  end normally, strike N`. Two in a row write `video-off` (reinstall, or remove `video-off` and
  `dm-strikes`, to clear). If that line appears after an ordinary ignition-off, tell the author:
  the guard would then switch the hook off after two trips.
- Shared memory with the libgal: since version 4 a 4 MB byte ring with an index of the last 512
  frames (`shim/src/cluster_video_shm.h`, host test `tests/ring`). Version 3 had 24 fixed slots of
  250 000 bytes, too small for the decode mode's key frames. libgal and libkb_dm.so of different
  versions do not talk to each other (stock map); install them together.
- Log: `/dev/shmem/aatokombi-dm.log` (at 64 KB it becomes `aatokombi-dm.log.1` and a new one starts). Counters (`rd_frames`, `rd_state`, ring waits) also appear in
  libgal's `/dev/shmem/aa_cluster_probe`.
- Frame capture (DEBUG builds: while `/dev/shmem/aa_debug` exists): the first 90 access units of
  each decode run, from the key frame on, go to `/dev/shmem/aa_cluster_au_<0..3>.h264` (Annex B).
  On a PC: `ffmpeg -i aa_cluster_au_0.h264 -frames:v 1 frame.png`.
- Pixel probe (opt-in, `/tsd/var/aatokombi/px-probe`, empty or `disp <display ID>`): while the
  decode mode plays, a display is copied with QNX Screen the way the firmware's `screenshot` does
  it, 1.5 s after the start and then every 5 s. The first copy of each stream saves every display
  (`/dev/shmem/aa_px_d<ID>.raw`; ID, type and size of each are in the log). The copies compared
  later come from `disp`, else from the first display that is not 800x480. Such a copy is kept
  when it is the first of the stream or the picture changed (half of a 16x9 grid moved, at most
  one copy per 30 s): the grid in the log, the whole picture as `/dev/shmem/aa_px_<0..5>.raw`.
  `tools/px-raw-to-png.py` makes PNGs of the `.raw` files. If DisplayManager dies during a copy,
  the probe stays off until the next reboot. Remove the file when done.
