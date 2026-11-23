# v0.1.0 Foundation

The reference build is Mason363/NumPlay 1.24.22 at `56b7f4bb4dfe8dafdcd83b3f82ffd32cfbd647f2`. The edition derives an ordered catalogue from the upstream records, retains the original preview and save definitions, and builds Doodle Jump from its XL module inputs.

On Ubuntu 24.04 install the prerequisites in `.github/actions/setup/action.yml`, then run:

```sh
NWLINK=nwlink make -j2 foundation
```

The output is `build/foundation/TwiliPlay.nwa`. The check reads the linked ELF game blocks and counts bytes in the installer image. It tests the full 2,555,904-byte app region reported upstream for N0120/Epsilon 26.4, with RAM budgets of 148,928, 153,676 and 133,652 bytes. The game arena is shared. Stack high-water marks and free capacity on an individual calculator require separate measurements.

The Foundation workflow builds the unmodified reference, runs host and ARM emulator tests, and uploads the checked package. Upstream GPL-3.0 source, notices and build recipes are retained.

## Verified N0120 build

[Foundation run 45283028381](https://github.com/Twil1ght-L4D2/TwiliPlay/actions/runs/45283028381) built source `693424ea4a7a73e4953814726ab5ee5a9d812d16` on Ubuntu 24.04. The clean 27-game reference installed as 2,011,136 bytes. The 18-game edition installs as 1,510,120 bytes, a reduction of 501,016 bytes. Epsilon 26.4's modeled app window is 0x90180000 through 0x903EFFFF, leaving 1,045,784 bytes on an otherwise empty app partition. The file size is 1,761,032 bytes. Device firmware and occupied flash require an owner check.

[Download the artifact ZIP](https://github.com/Twil1ght-L4D2/TwiliPlay/actions/runs/45283028381/artifacts/14870437774). The file inside is `TwiliPlay.nwa`, SHA-256 `edac307142c3941a0819105781fb5d7bb6a5a510f8d13454e04b18a42453c47f`.

| Installed flash component | Bytes |
|---|---:|
| Launcher code and resources, including name and icon | 52,380 |
| Shared libraries | 14,353 |
| Game metadata, including the 1,440-byte game table | 3,551 |
| Game block input sections, including 63,550 bytes of previews | 1,439,367 |
| Linker padding, including 65 bytes inside game blocks | 163 |
| Installer runtime, header and alignment | 306 |
| **Total** | **1,510,120** |

The launcher uses 1,180 bytes above the 50 KiB ideal and leaves 50,020 bytes below the 100 KiB ceiling. Linker attribution uses the base-object and final maps. A [separate rebuild](https://github.com/Twil1ght-L4D2/TwiliPlay/actions/runs/45282062518) compared the entire NWA byte for byte.

The installer-linked ELF places 464 bytes before the 98,304-byte arena. The largest game, NumDrive, needs 132,704 aligned arena bytes. The combined requirement is **133,168 bytes**, including the 34,400-byte extension used while the game runs. Installer links passed with 15,760 bytes to spare under 148,928 bytes, 20,508 under 153,676 and 484 under 133,652. These figures cover the app RAM region. The upstream emulator models a separate 32 KiB stack with a 30 KiB test guard. No selected game declares an additional minimum heap in the catalogue.

| Game in launcher order | Flash block incl. previews and padding | Preview bytes | Initial data bytes | BSS bytes |
|---|---:|---:|---:|---:|
| NumDash | 454,856 | 5,380 | 16 | 119,016 |
| Crossy Road | 29,392 | 7,964 | 0 | 98,196 |
| NumBlocks | 377,052 | 11,428 | 128 | 130,264 |
| NumDrive | 216,944 | 4,368 | 36 | 132,664 |
| Suika Game | 12,752 | 3,556 | 0 | 24,516 |
| Tetris | 44,216 | 2,628 | 0 | 389 |
| Chrome Dino | 17,952 | 1,736 | 4 | 33,584 |
| Doodle Jump | 122,392 | 2,384 | 0 | 82,156 |
| Chess | 25,624 | 4,060 | 48 | 75,944 |
| Flappy Bird | 18,880 | 2,028 | 8 | 21,836 |
| Tower Blocks | 5,932 | 648 | 0 | 11,156 |
| Core Ball | 5,648 | 984 | 0 | 10,556 |
| Pac-Man | 19,232 | 3,236 | 4 | 18,496 |
| Snake | 22,276 | 1,624 | 0 | 21,432 |
| Connect Four | 16,972 | 2,744 | 40 | 54,692 |
| 2048 | 17,200 | 3,852 | 12 | 27,946 |
| Minesweeper | 18,456 | 3,624 | 36 | 7,224 |
| Block Breaker | 13,656 | 1,306 | 8 | 11,832 |

Tool versions were xPack ARM GCC 15.2.1 with newlib nano, Node 20.20.2, nwlink 1.0.0, Python 3.12.16, Pillow 13.0.0, Rust and Cargo 1.100.0 with target `thumbv7em-none-eabihf`.

## Test coverage

The linked ELF contains exactly the 18 game blocks in the specified order. Each game launched through the carousel in the N0120 ARM emulator. All 18 executed code in their own module, exited on Home, released their locks, and cleared app RAM. NumBlocks recorded the highest observed stack usage at 23,516 bytes, leaving 9,252 bytes in the separate 32 KiB stack during these scenarios. Doodle Jump also passed two gameplay sessions exercising its native save script and recovery after a simulated reinstall.

All eight host suites passed for NumDash, NumBlocks, NumDrive, Tower Blocks, Suika Game, Doodle Jump, Core Ball and Crossy Road. The regression suite passed its checks for save backup, restore and reset, safe uninstall of NumDrive in the emulator, and behavior with insufficient RAM. For this edition, the build checks use the applicable installer operations from upstream `make check`; the full upstream target also builds every standalone app and other firmware variants. The [upstream Build run](https://github.com/Mason363/NumPlay/actions/runs/45100335094) passed at the pinned revision.

Physical installation, actual free space, extended gameplay, frame rates and hardware stability await owner testing. The measurements above use the device binary and ARM emulator; the separate desktop simulator was not run.
