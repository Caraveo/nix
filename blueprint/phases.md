# Phase plan and evidence

Follow the order in [Architecture and Codex phases](NIX_Architecture_Codex_Phases.pdf).
Do not start dependent implementation until its prerequisite acceptance gates are
met. Existing prototypes can inform the audit but do not waive those gates.

## Audited starting point

- Repository: `Caraveo/nix`; the checkout's `nix` remote points there. Its `origin`
  remote points to the separate website repository, `Caraveo/nix-site`.
- Committed runtime baseline: `5e2e407`, named kernel filesystem persistence.
- Development host: Intel Mac with 16 GiB physical RAM; external QEMU 11.1.3,
  TCG, AMD `phenom` CPU model. No physical NIX Station 1 boot is qualified.
- Core is a freestanding C/assembly kernel entered through a Multiboot stub and
  x86-64 long mode. Current scheduling is cooperative with interrupts masked.
- Native proof: ring-3 Author pod and a host framebuffer interface. General
  isolated Core services, IPC, supervision, and independent native surfaces are
  not yet qualified.
- Guest proof: Core's direct SVM/NPT prototype boots separate Linux 7.3.1
  instances with private RAM, devices, NAT, and named persistent filesystems.
  This is not QEMU running inside Core.
- Graphics in these tests: software-rendered Core UI through external QEMU's
  standard VGA framebuffer. Mesa/EGL, native OpenGL, VirGL, shared guest display
  surfaces, Windows compatibility, and physical GPU qualification are unproven.
- Pioneer and `core` work in the current working tree is prototype work beyond
  the committed baseline. Record its tests separately; it does not complete
  platform phases 2–8 or 9's surface/permission acceptance gate.

Reproducible baseline commands, after preparing the documented Linux userspace
and guest image dependencies:

```sh
sh scripts/build.sh
python3 tests/smoke.py
python3 tests/linuxfull.py
python3 tests/persistence.py
```

Observed baseline evidence: build passed; smoke covered boot/input, Memories,
recovery, and shutdown; Linux tests covered Bash, Python/threads/FPU, compilation,
timers, ping/curl/wget/DNS/HTTPS, and independent resumable guests. Persistence
tests retained an `/etc` file across a complete NIX/QEMU restart and verified
listing, reopening, and explicitly confirmed deletion. Keep test output attached
to the exact source revision being qualified.

## Delivery sequence

Paths below identify existing implementation touchpoints. New subsystems need
their own contracts and tests; listing a path is not a completion claim.

| Phase | Repository work and ownership | Acceptance/evidence required | Current state |
| --- | --- | --- | --- |
| 0. Repository audit | `README.md`, `AGENTS.md`, build scripts, `kernel/`, `user/`, `tests/`; inventory services, graphics, virtualization and licenses | Reproduce the current build; record source revision, commands, hardware, backend, and observed behavior | Baseline evidence above; refresh after working-tree changes land |
| 1. Architecture contracts | This directory; Core/NIX boundaries, service and virtualization interfaces, driver ownership, surface protocol | Every component has an owner, interface, dependencies, and failure behavior | Initial contracts recorded; review against executable designs before phase 2 |
| 2. Core execution foundation | `kernel/core.c`, `kernel/pod.h`; address spaces, scheduling, IPC, shared memory, handles, supervision | Two isolated services communicate; crash/restart one without corrupting its peer | Gate not met; next executable platform phase |
| 3. Native display foundation | `kernel/framebuffer.h`, input handling, new display/compositor service and client surface API | Two native surfaces display, resize, receive input, and recover from client failure | Existing UI is a prototype, not this acceptance gate |
| 4. Kernel virtualization layer | `kernel/linuxvm.h`, `kernel/linux_devices.h`, `kernel/instances.h`, `scripts/linux-*`; QEMU Core runtime/backend integration | Two apps have separate Linux 7.3.1 kernels and RAM; restart one without stopping its peer or Core; prove limits, acceleration/fallback and teardown | Direct SVM prototype exists; QEMU-on-Core integration remains unimplemented |
| 5. Guest display bridge | Compositor/surface API plus QEMU shared-frame adapter and input/resize/cursor bridge | Interactive guest appears through NIX's compositor on the host display | Gate not met; serial output in an app tab is not a guest surface |
| 6. Native OpenGL | Core graphics service, Mesa/EGL port, selected GPU driver, software fallback | Record renderer; pass selected desktop OpenGL 3.0 API tests on named hardware; test ES separately | Gate not met |
| 7. Accelerated guest graphics | Linux virtio-gpu/VirGL, GPU buffer import and explicit synchronization | Guest 3D inside NIX; measured copies/frame timing; renderer failure/recovery | Gate not met |
| 8. Windows applications/games | Separate Windows guest image, storage, network, display, input, audio; separate passthrough/capture evaluation | Compatibility matrix for named applications/games on specified hardware | Gate not met |
| 9. Prompt/application services | NIX launch broker, local prompt inference, Pioneer adapter, Ghost, Photon; narrow APIs and capabilities | Prompt launches a native/guest app and presents its surface with enforced permissions | Partial command/guest prototypes only; prior platform gates still required |
| 10. Release qualification | Installer, signed updates, rollback/recovery, resource limits, hardware matrix, license inventory | Install, update, recover, and run qualified workloads on supported machines | Gate not met; physical hardware work remains |

For each implementation change, update the relevant contract, add a meaningful
acceptance test, and record its exact result. Preserve software/copied-frame
fallbacks while bringing up acceleration. Do not infer graphics, Windows,
hardware, or release qualification from a successful command-line Linux test.
