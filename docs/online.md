# Online co-op

Each player uses their own copy of the same game and disc edition, their own profile and their own figures. The host is
Player 1; the guest is Player 2. Both players need the same launcher and installed game version. Sessions between systems
are off by default; both players can turn on **Cross-platform sessions** in the game's Online tab.

## Set it up: the game's Online tab

Online play is set up per game, in its settings (the gear beside PLAY), tab **Online**. **How PLAY starts the game** has
three choices: **Solo** (the game alone, as before), **Host** (PLAY opens a room) and **Join** (PLAY joins the friend's
room). The choice stays after a session, so the same friends play again in one press. The tab shows the four steps of
the chosen mode, then its fields: on Host, **Open my router automatically**; on Join, the
**Room code** the friend sent (hidden until **Show**). An address alone cannot open a room. **Details for your router** holds the technical
part: the port (47640, changed only when another program uses it), UDP and TCP, what the automatic opening does, and the
virtual network way (Tailscale, ZeroTier) for a router that cannot be opened. The settings live in `[<game>.online]` of
`launcher.ini` (`mode`, `port`, `open_port`, `address`, `cross_platform`). The old room password is ignored and removed when the Online settings are saved.

While a game is set to Host or Join, the sidebar shows `-o` after its version (`0.10.0-o`), as the game does in a
session. On the game page, PLAY then reads **HOST A ROOM** or **JOIN A ROOM**, and the line above it says which; pressing that line
opens the Online tab. Join without a room code opens the tab instead. The friend's code is never shown on the game page.

## The room

The host's room shows two cards side by side, the same height. The empty one invites: the room code to send (hidden
until **Show**; **Copy** works either way) and how far it reaches (see "Room codes"). When a friend elsewhere cannot get
in, the card says why, and **How to fix it** gives the ways: forward the port (UDP and TCP) in the router's settings
(when the router refused or automatic opening is off), a virtual network on both computers, and the code for a friend in
the same home (the computer's local address). Opening the router and finding the public address take a few seconds; the
window stays usable.

The room's **Details** offers **Code for another address**: enter this computer's virtual-network address
(such as Tailscale or ZeroTier), with its port, then **Copy** the resulting code. It carries the same room secret
as the public and home codes. Share a code only with the friend you want to let in.

Each player's card shows their name (yours followed by "(you)"), picture, colour, role, readiness and the figures on
their portal, with **Choose
figures** (Collection) and, once both are in, **I'm ready**. Each player brings their own Player 1 figures; the host also
brings the shared figures. The guest's figures occupy Player 2's seats in the game. The connection (good, fair or slow,
the round trip and the input delay) appears once the guest is in; the host's frame rate and game languages head the room.
Beside **Start** (host only) and **Leave**, one sentence says what the room waits for. **Leave**, Back, or changing page
closes the room. A refused guest leaves a notice in the host's room.

When something fails, the room shows a help card: what happened, both sides' values where they differ (launcher and game
versions, systems, games), what to do, and a button that does it (**Try again**, **Open the Online tab**, **Go to the
game's page** for its update, **Check for updates** for the launcher's).

During play, the game page says **Online with <pseudo>**. Your figures' progress returns to your own Toy Box after the game;
the host keeps the shared save progress. The guest's previous saves stay their own. If you close the launcher during a
session, reopening it waits for the game to finish before recovering the session's files.

In a game started alone, **Play online** in the F3 menu closes the game and opens this game's Online tab (the game
leaves `open-online.txt`, its id, in its saves; the launcher reads and removes it when the game ends).

## Room protocol

`src/launcher/online/lobby_protocol.h` defines protocol version 2. One TCP connection carries little-endian frames:
`u32 length`, `u8 type`, `u32 sequence`, payload, a 16-byte authentication tag. Frames are at most 4 MB. Text is prefixed
by a 16-bit byte count, blobs by a 32-bit count. Hellos carry random 32-byte nonces, fresh for each connection. The room's 40-bit secret, as five bytes in
most-significant-byte order, and both nonces derive separate HMAC-SHA256 tag keys and ChaCha20 encryption keys
for each direction. Every payload after the hellos, including proofs, saves and Start, is encrypted before its tag
is computed. The tag covers the type, sequence and ciphertext and is checked before decryption. The frame nonce
is 12 bytes: a little-endian direction word (1 host, 2 guest), the little-endian sequence, then four zero bytes;
the ChaCha20 block counter starts at 1. A sequence must be exactly the next one; a connection closes before the
32-bit sequence can wrap. Headers and hellos are visible; room contents are private.

The host counts failed proofs in a sliding 60-second window. The fifth starts a 30-second pause for all new guests,
including guests with a correct code, with a refusal notice in the room. Existing size, player, seat and save checks
still apply. A rejected guest leaves the host's room open. Full-room and paused-room refusals before the hellos are
unauthenticated; after the hellos every frame requires authentication and encryption.

| Type | Value | Payload and purpose |
|---|---|---|
| Hello | 1 | Protocol, host flag, launcher version, game id, executable SHA-256, game version, platform, cross-platform flag, nonce; compare compatibility |
| Proof | 2 | 16-byte proof of the secret-derived directional key |
| Player | 3 | Pseudo, Xbox id, colour, PNG picture (at most 512 KB), up to 16 figures with type, variant, seat, name and dump bytes |
| Settings | 4 | Frame rate, text and voice languages, Nightmare option, total save bytes and file count |
| SaveChunk | 5 | Relative file path, whole file length, offset, bytes; chunks at most 64 KB |
| Ping | 6 | Timestamp for a round-trip measurement |
| Pong | 7 | The received timestamp echoed back |
| Ready | 8 | Player's ready flag; changing the player's figures clears readiness |
| Start | 9 | 16-byte session key, input delay in frames, host's UDP port |
| Bye | 10 | Left, full, mismatch, code, protocol, timeout or attempts-limit reason |

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

## Room codes

A room code holds the host's address and a fresh random 40-bit secret chosen with `RandomBytes` when the room opens: no server holds rooms. Closing and reopening the room invalidates its old codes. The secret is required to enter, even when the address and port are known. `src/launcher/online/room_code.h`
writes it in Crockford's base 32 (`0-9` and `A-Z` without `I L O U`; on reading, case, dashes and spaces are ignored and
`O`, `I`, `L` read as `0`, `1`, `1`), in groups of four, with a check made from 64-bit FNV-1a over the kind and the address, optional port and five secret bytes (most significant first):

| Letters | Holds | Check |
|---|---|---|
| 20 | IPv4 address, secret; port 47640 | 28 bits |
| 24 | IPv4 address, port and secret | 32 bits |
| 44 | IPv6 address, port and secret | 36 bits |

`net::ParseAddress` still reads codes and plain addresses. Joining requires `DecodeRoomCode` to recover the secret
before opening a socket; plain addresses show a help card asking for the code. The game's `SKY_NETPLAY_JOIN` remains
an address and port. Which address the code holds (`InviteFor` in `src/launcher/ui/online_page.cpp`):

| The room's card | When | The code holds |
|---|---|---|
| lets friends in from anywhere | the router opened the port on a public IPv4 address | that address and the router's port |
| uses IPv6 | no public IPv4 address opened, automatic opening on, this computer answers over IPv6 | the IPv6 address, the room's port |
| shared by the provider | the router opened the port, but its own address is private (CGNAT, a second router) | the public IPv4 address |
| refused | the router refused | the public IPv4 address, for a port forwarded by hand |
| not opened automatically | automatic opening off | the public IPv4 address |

The public addresses come from one STUN binding request (RFC 5389, `src/launcher/online/stun.h`) per room, over IPv4
and over IPv6, to `stun.l.google.com:19302`, else `stun.cloudflare.com:3478`; only the mapped address of the answer is
read. Without an answer the code holds the computer's local address.

## Play together again

When a session stops, the room is set up again with the same code once the saves and figures are back. The game's
banner (R or Y while it waits for the other player) and its F3 menu after a session ("Play together again") quit with
`online-replay.txt` in the session's saves: the host's launcher reopens its room with the last secret and port, the
guest's joins the code it has, again every 3 seconds for 2 minutes while the host's room is not there yet. A game that
stopped without writing `online-ended.txt` (a crash; closing the window writes `quit`) opens the room's page with
"Your game stopped during the session" and **Try again**, which does the same. Both start from the host's last save.

## The log

The room writes `[online]` lines to the launcher's output, in words and never with an address (players share their
logs): the room opening and its port; the router's answer (`port 47640 opened with UPnP (outside port 47640)`, or why
not); whether STUN found an IPv4 and an IPv6 address; the room code's reach when it changes; a guest joining (IPv4 or
IPv6) and the other player's launcher, game, version and system; each side's readiness; the round trip and the input
delay every 10 seconds; the start (input delay, round trip, frame rate, save size); and every end or refusal with its
reason and, for a mismatch, both sides' values (`game version: 0.10.0 here, 0.9.17 there`).

## Router lease

The room's `RouterLease` worker calls `netplay::portmap::Map` with `Options::alsoTcp = true` (not with automatic opening
off): the room needs TCP and the game needs UDP on the same port. UPnP is tried first, then NAT-PMP. The lease is renewed
every hour. Then, once per room, the worker asks the public addresses (`stun::Lookup`), not for a room already closed. Closing the room or
ending the game schedules `Unmap` on the same worker. If a room closes while mapping is in flight, the returned mapping
is removed before the worker handles another room. The worker joins at window shutdown.

## Checks and screenshots

`OnlineUiChecks` in `tests/online_ui_test.cpp` checks the state adapter with generated figures and stand-in save bytes,
startup and game-exit recovery, host and guest write-back, every end reason, and a failed recovery followed by retry.
`SKY_LAUNCHER_TEST_ONLINE_UI_ONLY=1` selects it in `ArkeyanLauncherTest`. The existing `OnlineChecks` cover the room protocol,
loopback peers, port mapping against local fixtures, room codes (every length, loose reading, typos) and STUN answers,
and session preparation and write-back. Security checks include the RFC 8439 section 2.3.2 and 2.4.2 vectors,
wire captures without known profile/save plaintext, altered ciphertext, replay, failed-proof windows and cooldown,
and oversized players, invalid save chunks and unknown messages followed by a successful join.

With scratch HOME, settings, data and collection folders, `SDL_VIDEO_DRIVER=offscreen`, `SKY_LAUNCHER_SHOTS=<scratch shots>`
and `SKY_LAUNCHER_TEST_ONLINE_UI=1` capture the Host and Join tabs, waiting and ready rooms, every room error and mismatch,
and input, launch and recovery failures, the guest view, refusal notice, game button, launcher setting and every session
end toast (including a silent normal end). Set `SKY_LAUNCHER_LANGUAGE=en` or `fr` for each language and
`SKY_LAUNCHER_TEST_SIZE=1024x680` or `1600x1000` for each size. The optional `OnlineRoom::preview`
status is only set by this screenshot script; it bypasses sockets, launch and router mapping. No game disc is needed. The pad walk's `51-online-host` through `55-online-guest` views check every enabled control,
reading order, Back and the form tabs; select them with `SKY_LAUNCHER_TEST_PADWALK_VIEW=51-,52-,53-,54-,55-`.
The router-worker checks inject callbacks, including a late mapping result after Leave, a short renewal interval and the
public address lookup; they never contact a router or a STUN server. `InviteFor` is checked for every row of "Room codes".
