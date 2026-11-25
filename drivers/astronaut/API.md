# Astronaut driver

Which cow is in a milking robot's box, from the robot's own messages. It serves the identity
contract (`core/identity_c.h`): the robot is one place of the kind `milkingRobot`, with the
robot's address as its id. Link: `udp`, port 6000 on the robot. The driver is `src/Astronaut.cpp`.

## How

The robot's own computer (on an A4: ARM, Linux) runs a program `Proxy` on UDP port 6000 that hands
every internal LFW message to the clients registered with it. The farm software T4C registers
the same way ("LogService").

| Datagram | Format (little-endian) |
|---|---|
| Register (renew every 2 s) | `u32 0xD157`, `u16 1`, client name (22 bytes, NUL-padded) |
| Unregister | the same with `u16 0` |
| A message from the proxy | `u32 0xD156`, `u16 message`, `u16 destination`, `u16 sender`, `u16 length`, `length` bytes |

Three messages are read:

| Message | When | Data | Event |
|---|---|---|---|
| `0x116` (MSG_REQUEST_COW_NR_NAME) | the robot has read a cow's responder at the start of a visit | `u32 responder`, `u32 cow number`, the name in UTF-16LE up to a NUL | `identified` |
| `0x002` (start milking) | the cups go on | the teat positions (38 bytes or more) | `milkingStarted` |
| `0x402` (visit end) | `data[0]` = 1: milking done, the cow may leave (about 8 s after the last cup is off); `data[0]` = 0 about 30 s later | | `visitEnded` (on 1 only) |
| `0x3F7` (MSG_DB_COLINFO), table 18 | a cow came before her milking was due: the robot records the refusal for the farm software, 6–12 s after identifying her | `u16 table` at data[6] (18), `u32 responder` at data[8] (32 bytes in all) | `refused` |

A cow identified at robot 1 without a milking and without a refusal walked through to robot 2 (a
TCS passage). In a 45-minute recording of both robots every identification without a milking at
robot 2 had its table-18 record with the same responder; at robot 1 so had the cows that stayed, not
the ones that walked through.

Any other message only counts as "the robot is there". The destination and sender in the header
are mailboxes inside the robot (e.g. 17 is the robot's screen); the driver does not need them.

The cow milked is the one identified closest in time to the start of the milking (within a minute),
not simply the last one identified: see the TCS passage below. On the robots checked, the cow in the
box is identified at the very moment the cups go on (`0x116` and `0x002` in the same second), a cow
walking past some seconds before or after. The driver tells `milkingStarted` 10 s after the start
(with the start as its time) so that an identification arriving with it counts. A second start
within two minutes is the same milking.

The driver never sends an LFW message (`0xD156`) itself: what it sends is its registration and
nothing else, so it cannot change anything on the robot.

The robot's own computer is not a server meant for other programs, as a farm's management server
is: this driver is **experimental** until it has run for longer than the check below.

## Checked on real robots

Two Astronaut A4s (2026-11-23, software Av8.0), five minutes, next to another program reading the
same proxy: every identification came within the same second as that program saw it, number, name and
responder alike. Several clients can be registered with one proxy at the same time.

- The robot does not say when a cow leaves: a refused cow walks on without a message. The animal
  stays the robot's current animal until the next one.
- With a TCS passage (robot 1 leads to robot 2), robot 1 also sends `0x116` for the cows that walk
  through to robot 2: such a cow is identified at robot 1 and a minute later at robot 2. The start
  of the milking puts the cow in robot 1's box back as the place's animal.
- `0x002` and `0x402` (with 1, then 0) were checked against a camera at the robots' exits and the
  milking results on 24 November 2026: one of each per milking, the 1 about 8 s after the cups came off.
- A recording of both robots (22 November 2026, 45 minutes, 7 milkings) played through the driver:
  every milking and visit end had the cow that was milked, also with cows walking past robot 1.
- The same cow twice within 5 s is one identification.

## Not checked yet

- The Astronaut A5 and older A3 robots: whether they have the same proxy and message.
- A robot behind a router without UDP replies: the registration needs the robot's answers to come
  back to the sender's port.
