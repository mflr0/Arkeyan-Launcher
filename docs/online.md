# Online co-op

Each player uses their own copy of the same game and disc edition, their own profile and their own figures. The host is
Player 1; the guest is Player 2. Both players need the same launcher and installed game version. Sessions between systems
are off by default; both players can enable **Allow cross-platform sessions (not tested)** in Settings > Launcher.

## Open or join a room

Choose an installed game and press **Online** beside PLAY. On **Host**, leave the port at 47640 or choose another, choose
whether to open the router's port automatically, and optionally set a password. Press **Open room**, then **Copy** beside
the address and give it and the password to the other player. On **Join**, paste that address into **Address**, enter the
password if needed, and press **Connect**.

The host's address uses the public IPv4 address reported by the router when available. Otherwise it shows the computer's
local address and asks you to open the displayed port for both UDP and TCP on the router. The local address works for
another computer on the same network; someone elsewhere needs your router's public address. Automatic opening can take
several seconds; the window stays usable. An address behind a second router may need forwarding on both routers.

The room shows both players' names, pictures, colours and figures, the round trip and input delay, and the host's frame
rate and game languages. Choose your figures in Collection before opening the room: each player brings their own Player 1
figures; the host also brings the shared figures. The guest's figures occupy Player 2's seats in the game. Each player
switches **Ready** on, then the host presses **Start**. **Leave**, Back, or changing page closes the room. A refused guest
leaves a notice in the host's room. An error explains what to check before trying again.

During play, the game page says **Online with <pseudo>**. Your figures' progress returns to your own Toy Box after the game;
the host keeps the shared save progress. The guest's previous saves stay their own. If you close the launcher during a
session, reopening it waits for the game to finish before recovering the session's files.

In a game started alone, **Play online** in the F3 menu closes the game and opens this game's Online page (the game
leaves `open-online.txt`, its id, in its saves; the launcher reads and removes it when the game ends).

## Room protocol

`src/launcher/online/lobby_protocol.h` defines protocol version 1. One TCP connection carries little-endian frames:
`u32 length`, `u8 type`, `u32 sequence`, payload, a 16-byte authentication tag. Frames are at most 4 MB. Text is prefixed
by a 16-bit byte count, blobs by a 32-bit count. Hellos carry random 32-byte nonces; the password and both nonces derive
separate HMAC-SHA256 keys for each direction. The proof and subsequent frames authenticate those keys.

| Type | Value | Payload and purpose |
|---|---|---|
| Hello | 1 | Protocol, host flag, launcher version, game id, executable SHA-256, game version, platform, cross-platform flag, nonce; compare compatibility |
| Proof | 2 | 16-byte proof of the password-derived directional key |
| Player | 3 | Pseudo, Xbox id, colour, PNG picture (at most 512 KB), up to 16 figures with type, variant, seat, name and dump bytes |
| Settings | 4 | Frame rate, text and voice languages, Nightmare option, total save bytes and file count |
| SaveChunk | 5 | Relative file path, whole file length, offset, bytes; chunks at most 64 KB |
| Ping | 6 | Timestamp for a round-trip measurement |
| Pong | 7 | The received timestamp echoed back |
| Ready | 8 | Player's ready flag; changing the player's figures clears readiness |
| Start | 9 | 16-byte session key, input delay in frames, host's UDP port |
| Bye | 10 | Left, full, mismatch, password, protocol or timeout reason |

The hello uses `LauncherVersion()` (the build's `LAUNCHER_VERSION_STRING`), the verified page's executable SHA-256 and
`FindInstalledGame(...).gameVersion`. Platform is the OS and architecture, for example `macos-arm64` or `linux-x64`.
`room_inputs.cpp` resolves languages with the same `GameLanguageToPass` helper as normal PLAY. Save input includes every
regular file below the game's saves folder, with relative slash-separated names; portal seats and backups are excluded.
Symlinks are refused and the total is limited to 64 MB. The profile picture and each figure's source file accompany the
player description; the source paths stay local and are used only for write-back.

## Starting and recovery

`PrepareGameLaunch` builds the normal launch; `PrepareSession` adds the agreed room to it. The guest's host address is the
address it joined with, without the port; the guest sends UDP to that address's port, including when the router assigned a different external port. The session lives at `<data>/online/session/`:

- `saves/`: the host's save and the session's `portal.txt`.
- `figures/`: the figures from both players; a swapper's bottom half is `<top>-bottom.sky`, seated in the slot after
  its top and written back to the collection file its top names as its partner.
- `pictures/`: both profile pictures when supplied.
- `session.txt`: the local write-back manifest, including the local figure files.

`<data>/online/destination.txt` records the local save destination, game id and other player's pseudo for recovery after a launcher restart. No new online
launch may replace a session pending recovery. A game's session save marker is checked before recovering it.

| Variable | What the game receives |
|---|---|
| `SKY_SAVE_DIR` | Session's saves folder |
| `SKY_PORTAL` | `emulated`; the previous `SKY_PORTAL_PLACE` is removed |
| `SKY_PLAYER_NAME`, `SKY_PLAYER_XUID`, `SKY_PLAYER_PICTURE` | Host's identity and session picture |
| `SKY_PLAYER2_NAME`, `SKY_PLAYER2_XUID`, `SKY_PLAYER2_PICTURE` | Guest's identity and session picture |
| `SKY_NETPLAY_FPS` | Host's frame rate |
| `SKY_LANGUAGE`, `SKY_VOICE_LANGUAGE` | Host's languages, absent when using the game defaults |
| `SKY_NIGHTMARE_FROM_START` | Host's choice, `1` or `0` |
| `SKY_NETPLAY_DETERMINISTIC` | `1` |
| `SKY_NETPLAY_DELIVERY` | `frame` |
| `SKY_NETPLAY_SCHEDULE` | `cooperative` |
| `SKY_NETPLAY_HOST` | Host only: UDP port |
| `SKY_NETPLAY_JOIN` | Guest only: host address and UDP port |
| `SKY_NETPLAY_PASSWORD` | Hex session key agreed by the room |
| `SKY_NETPLAY_DELAY` | Agreed input delay in frames |

After `GameSession::Poll` reports the game's end (excluding a graphics-backend restart), `FinishSession` writes every file of
the session's `saves/` back to the host's real saves (new ones too; not the session's `portal.txt`, `online-ended.txt`
or `open-online.txt`), backing up overwritten versions in `<saves>-backup`, and writes the local player's
figures back to their collection files. The manifest is removed after success, so recovery can be called again. Startup
also runs recovery, deferring it while the session's game marker is live. Errors leave the session available for another
recovery attempt.

Recovery also reads `saves/online-ended.txt` (`reason=<code>`). A missing reason or `quit` is silent; `timeout`, `desync`,
`transport`, `protocol`, `game`, `image`, `runtime`, `platform`, `fps`, `seats` and `invalid` show one translated error
toast explaining what happened and what to do. Startup resolves the launcher's language before making the toast.
Unknown codes use a general retry message. Successful recovery removes the end marker along with the destination record,
so another recovery does not repeat the toast. Failed recovery keeps both records for retry, including after leaving
the room.

## Router lease

The room's `RouterLease` worker calls `netplay::portmap::Map` with `Options::alsoTcp = true`: the room needs TCP and the
game needs UDP on the same port. UPnP is tried first, then NAT-PMP. The lease is renewed every hour. Closing the room or
ending the game schedules `Unmap` on the same worker. If a room closes while mapping is in flight, the returned mapping
is removed before the worker handles another room. The worker joins at window shutdown.

## Checks and screenshots

`OnlineUiChecks` in `tests/online_ui_test.cpp` checks the state adapter with generated figures and stand-in save bytes,
startup and game-exit recovery, host and guest write-back, every end reason, and a failed recovery followed by retry.
`SKY_LAUNCHER_TEST_ONLINE_UI_ONLY=1` selects it in `ArkeyanLauncherTest`. The existing `OnlineChecks` cover the room protocol,
loopback peers, port mapping against local fixtures, and session preparation and write-back.

With scratch HOME, settings, data and collection folders, `SDL_VIDEO_DRIVER=offscreen`, `SKY_LAUNCHER_SHOTS=<scratch shots>`
and `SKY_LAUNCHER_TEST_ONLINE_UI=1` capture the Host and Join tabs, waiting and ready rooms, every room error and mismatch,
and input, launch and recovery failures, the guest view, refusal notice, game button, launcher setting and every session
end toast (including a silent normal end). Set `SKY_LAUNCHER_LANGUAGE=en` or `fr` for each language and
`SKY_LAUNCHER_TEST_SIZE=1024x680` or `1600x1000` for each size. The optional `OnlineRoom::preview`
status is only set by this screenshot script; it bypasses sockets, launch and router mapping. No game disc is needed. The pad walk's `51-online-host` through `55-online-guest` views check every enabled control,
reading order, Back and the form tabs; select them with `SKY_LAUNCHER_TEST_PADWALK_VIEW=51-,52-,53-,54-,55-`.
The router-worker checks inject callbacks, including a late mapping result after Leave and a short renewal interval;
they never contact a router.
