# Pioneer and persistent NIX intents

The implementation target follows the owner's requests and the system design at
https://core-nix.com/hard and https://core-nix.com/architecture. The owner's newer
requirement for a paid, Cloudflare-backed Intent model replaces the older pages'
local-only inference requirement for Pioneer.

Required behavior:

- `ask pioneer` opens `#pioneer`, running a Pioneer-branded OpenCode engine inside
  a Linux runtime, with 4 GiB RAM and 20 GiB persistent storage.
- `ask kernel -n name` and `ask pioneer -n name` create or reopen named instances.
  Their filesystems survive closing the tab and restarting NIX. The built kernel
  and userspace are immutable base templates; `again` creates a fresh clone.
- `ask list` lists saved and live instances; explicit open, stop, and delete
  commands manage them. Deletion requires a specific confirmation.
- `core username/project` is the build/orchestration/deployment command. It checks
  out the associated GitHub project in an isolated persistent instance and uses
  paid AI units. Its `@project-name` tab sits after Prompt/Terminal and before
  `#` app tabs. Build results have manifests, verification, hashes, atomic
  activation and a rollback point.
- Pioneer has one AI provider: the NIX Intent API. Only an active paid Intent
  access token can use AI. Credits are checked and charged on the server.
  Cloudflare credentials never reach a guest, browser, or model prompt.

Implementation gates (not completion claims):

1. Per-tab 24-line view and scrollback — implemented and acceptance-tested in
   `tests/consolescroll.py`, committed as `4b4dc8b`.
2. Named instance registry and persistent isolated filesystems.
3. 4 GiB guest memory maps; persistent 20 GiB block devices.
4. Pinned Pioneer/OpenCode engine, single-provider restriction and local tools.
5. Paid Cloudflare Agent API, atomic credit reservation/refund, replay protection.
6. Named Pioneer setup/reopen, multi-instance and restart tests.
7. GitHub `core` workflow, artifact receipts, activation and rollback tests.
8. Continue architecture phases with explicit evidence for each runtime and
   hardware path. Physical Station 1 boot cannot be claimed without that machine.

Current engineering notes:

- Core repo: `/Users/caraveo/Documents/ChatGPT/Core-NIX`, remote `nix` is
  `Caraveo/nix`; `origin` is the website repo. Preserve Memories and the old ISO.
- Website/API: clean current source is cloned into `build/pioneer/site` from
  `Caraveo/nix-site` main, including the live Pioneer/Simulation pages. The sibling
  `../nix-site` checkout is dirty and stale; preserve it.
- Existing payments use Authorize.Net, not Stripe. D1 `nix-ask` already has
  subscriptions, paid access tokens, units, unit purchases and a unit ledger.
  Extend these; do not create a parallel payment system or charge a real card in
  tests. API secrets remain Cloudflare Worker secrets.
- OpenCode is pinned at upstream `v1.19.5` in `build/pioneer/opencode`; its dev
  branch is 2.x and is not the release being integrated. Retain upstream licenses.
- First dependency install in `oven/bun:1.4.4` failed because native tree-sitter
  packages need a compiler/Python. The Pioneer build Dockerfile supplies them.
- Linux block/NBD/ext4 support was enabled by `scripts/linux-kernel-full.sh`;
  build log `build/linux/pioneer-kernel-build.log` completed kernel build #5.
  OverlayFS was enabled in build #6 for whole-filesystem persistence.
- Proposed block path: a private NBD socket inside Linux proxies the existing
  guest ATA PIO interface; NIX maps each named disk into its own 20 GiB slice of
  a separate secondary IDE store. Never expose the primary Memories disk.
- Host now identity-maps 64 GiB; `user_pt()` moved to `0x1c2000`. The NIX
  smoke test passed after that change. Linux NPT still needs expansion for
  4 GiB Pioneer guests. Preserve the separate 2 MiB build guest and host syscall
  MSR restoration.
- The Mac has 16 GiB physical RAM. Budget tests accordingly; do not eagerly run
  four 4 GiB guests plus builds on it. Station 1 target has 32 GiB.
- Workers AI catalog includes `@cf/qwen/qwen3.8-27b` and
  `@cf/qwen/qwen3-30b-a3b-fp8`, both documented with tool calling. Select the model
  only on the server and expose the public alias `pioneer`.

In-progress persistence implementation:

- `kernel/instances.h`: two-bank checksummed registry on a secondary IDE disk
  with serial `NIX-PIONEER-STORE`; six isolated 20 GiB slots. Memories is separate.
- `user/nix_block.c`: guest NBD-to-private-ATA bridge, compile checked with GCC
  `-Wall -Wextra -Werror`. The bridge is compiled outside the guest image.
- PID 1 mounts each named instance as an OverlayFS writable root over the base;
  only a newly allocated registry entry may format its ext4 disk.
- Named kernel lookup, list, open, and explicitly confirmed deletion are wired.
  Restart persistence acceptance test `tests/persistence.py` passed: `/etc` file
  survives stopping and restarting NIX/QEMU; list/reopen and confirmed deletion
  pass. Guest REP I/O now injects normal demand/COW page faults into Linux.
- Pioneer baseline glibc engine `--version` smoke test passed, version 1.19.5.
  The build's subsequent musl target failed due to an absent optional native
  dependency. The glibc engine exists; guest execution is not yet verified.
