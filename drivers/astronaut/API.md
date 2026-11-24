# Astronaut live driver

Which cow is in a milking robot's box, from the robot's own messages (`core/live_c.h`). Link: `udp`,
port 6000 on the robot. The driver is `src/Astronaut.cpp`.

## How

The robot's own computer (on an A4: ARM, Linux) runs a program `Proxy` on UDP port 6000 that hands
every internal LFW message to the clients registered with it. The farm software T4C registers
the same way ("LogService").

| Datagram | Format (little-endian) |
|---|---|
| Register (renew every 2 s) | `u32 0xD157`, `u16 1`, client name (22 bytes, NUL-padded) |
| Unregister | the same with `u16 0` |
| A message from the proxy | `u32 0xD156`, `u16 message`, `u16 sender`, `u16 destination`, `u16 length`, `length` bytes |

One message is read: `0x116` (MSG_REQUEST_COW_NR_NAME), sent when the robot has read a cow's
responder at the start of a visit: `u32 responder`, `u32 cow number`, the name in UTF-16LE up to a
NUL. Any other message only counts as "the robot is there" (`connected`).

The driver never sends an LFW message (`0xD156`) itself: what it sends is its registration and
nothing else, so it cannot change anything on the robot.

The robot's own computer is not a server meant for other programs, as a farm's management server
is: this driver is **experimental** until it has run for longer than the check below.

## Checked on real robots

Two Astronaut A4s (2026-11-23, software Av8.0), five minutes, next to another program reading the
same proxy: every arrival came within the same second as that program saw it, number, name and
responder alike. Several clients can be registered with one proxy at the same time.

- The robot does not say when a cow leaves: a refused cow walks on without a message. The animal
  stays the robot's current animal until the next one.
- With a TCS passage (robot 1 leads to robot 2), robot 1 also sends `0x116` for the cows that walk
  through to robot 2: such a cow arrives at robot 1 and a minute later at robot 2.
- The same cow twice within 5 s is one arrival.

## Not checked yet

- The Astronaut A5 and older A3 robots: whether they have the same proxy and message.
- A robot behind a router without UDP replies: the registration needs the robot's answers to come
  back to the sender's port.
