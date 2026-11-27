# v0.1.0 Foundation

The reference build is Mason363/NumPlay 1.24.22 at `56b7f4bb4dfe8dafdcd83b3f82ffd32cfbd647f2`. The edition derives an ordered catalogue from the upstream records, retains the original preview and save definitions, and builds Doodle Jump from its XL module inputs.

On Ubuntu 24.04 install the prerequisites in `.github/actions/setup/action.yml`, then run:

```sh
NWLINK=nwlink make -j2 foundation
```

The output is `build/foundation/TwiliPlay.nwa`. The check reads the linked ELF game blocks and counts bytes in the installer image. It tests the full 2,555,904-byte app region reported upstream for N0120/Epsilon 26.4, with RAM budgets of 148,928, 153,676 and 133,652 bytes. The game arena is shared. Stack high-water marks and free capacity on an individual calculator require separate measurements.

The Foundation workflow builds the unmodified reference, runs host and ARM emulator tests, and uploads the checked package. Upstream GPL-3.0 source, notices and build recipes are retained.
