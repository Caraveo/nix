# Architecture contracts

Source: the two [adopted PDFs](README.md). These are implementation contracts;
interfaces described below are targets unless identified as existing behavior.

## System boundaries

| Component | Owner and interface | Dependencies | Failure behavior |
| --- | --- | --- | --- |
| Core OS | Trusted kernel; address spaces, scheduling, IPC, memory, handles, hardware virtualization | Physical CPU and qualified platform drivers | Revoke a failed process's handles and mappings; isolate its peers |
| Core platform services | Supervised storage, network, audio, and driver-domain services; narrow capability APIs | Core isolation, IPC, shared memory, supervision | Restart independently; reject stale handles; preserve committed data |
| NIX host experience | Prompt, Pioneer adapter, Ghost, Photon, and launch policy | Core service APIs and compositor | Keep the prompt usable when an app, model, or service fails |
| Native applications | Processes using Core service APIs directly | Core execution and native graphics contracts | Tear down their resources without stopping other applications |
| Kernel virtualization layer | Trusted VM lifecycle and resource broker; QEMU host integration | Core process/runtime support, storage, devices, virtualization backend | Stop/restart one VM; retain named storage; leave Core and peers running |
| Linux application guests | Separate Linux 7.3.1 kernel, RAM, dependencies, and filesystem per non-native app | Pinned guest templates and explicit resource grants | Contain faults and resource exhaustion within the guest |
| Windows guests | Separate Windows VM and qualified device/display path | Windows installation/licensing and independent compatibility tests | Fail independently; do not claim Linux/VirGL compatibility proves Windows support |
| NIX compositor | Host-owned native and guest surfaces, composition, focus, input, resize | Core display/input services and shared memory | Reclaim failed clients' surfaces; preserve other surfaces and input routing |
| Local AI service | Advisory prompt interpretation and launch proposals | Local model/runtime and narrow NIX service APIs | No autonomous authority to grant privileges; prompt remains usable offline |
| Pioneer Intent service | Paid NIX model API and credit accounting on Cloudflare; client uses an Intent token | Verified paid access, server credit ledger, private Cloudflare Agent/AI bindings | Reject unpaid/expired access; avoid duplicate charges; refund failed inference |

NIX is not a Linux distribution. Running the development image in QEMU does not
make Linux the Core host, and does not establish that QEMU runs as a Core service.

## Intent and VM lifecycle

- An intent can represent an operating system, container, pod, service,
  application, game, or another workload. A guest kernel is an execution mechanism.
- Keep built kernel/userspace templates immutable. A new instance starts from the
  base, with private RAM and devices. Named instances retain their writable
  filesystem; reopening a name must not replace it with a fresh template.
- Manage instances through explicit identities and lifecycle operations: create,
  open/resume, stop, list, and confirmed delete. `again` creates a new base clone.
- Pin Linux 7.3.1 and record image provenance. Give each application an explicit
  RAM/storage/device/network profile; teardown must release transient resources.
- Preserve the dedicated Memories disk separately from guest instance storage.
  Deleting an instance is not permission to erase unrelated Memories.
- Pioneer targets 4 GiB RAM and 20 GiB persistent storage per instance. Its AI
  provider is exclusively the paid Intent API; server credentials stay on Cloudflare.
- `core username/project` is an orchestration/build/deployment operation for that
  GitHub project. It uses paid AI units, an isolated guest workspace, verification,
  artifact receipts, atomic activation, and rollback. Its `@project` tab precedes
  `#` app tabs, after Prompt and Terminal. This remains dependent implementation
  work, not evidence that the underlying platform phases are qualified.

## Graphics and display protocol

Desktop OpenGL 3.0 is the baseline. OpenGL ES 3.0 is a separate API and needs
separate qualification. Keep software rendering and copied frames available.

The first guest display bridge imports shared-memory frames into host-owned
native surfaces. Use QEMU D-Bus display as a reference integration, not as a
claim that its dependencies are already available on Core. Accelerated import
follows only after the copied-frame protocol and lifetime rules work.

Proposed version-1 surface contract:

| Field/operation | Contract |
| --- | --- |
| Identity | Host-issued surface handle plus generation; scoped to its owning client/VM |
| Buffer description | Format, width, height, stride, byte length, and mapped region handle; validate overflow and bounds |
| Initial format | XRGB8888, little-endian, four bytes per pixel; stride at least width × 4 |
| Ownership | Host allocates/grants buffers; a client accesses only its granted mappings |
| Submit | Monotonic frame sequence, buffer identity, and damage rectangles; validate each rectangle |
| Synchronization | Explicit acquire/completion and release signals; no producer reuse before release |
| Resize | Allocate a new generation; keep the previous visible frame until the replacement is ready |
| Focus/input | Host chooses the focused surface and routes keyboard, pointer, cursor, and resize events |
| Client failure | Revoke its handles and pending frames; release mappings and preserve peers |
| Diagnostics | Record backend, copy count, frame timing, and input latency on named hardware |

GPU-buffer import must preserve the same ownership and synchronization model.
Linux DMA-BUF is not automatically a Core interface: implement an equivalent
Core contract or a compatible, isolated driver-domain bridge. Linux virtio-gpu /
VirGL, Windows display / Direct3D, and passthrough capture are separate paths.

## Authority and release rules

Model output is a proposal, not a capability grant. Build tools run inside their
assigned workspace/runtime. The trusted broker independently validates plans,
verification results, artifact hashes, and activation/rollback receipts. Guest
software must not directly install Core kernels or drivers.

Keep implemented behavior, architecture targets, test results, and hardware
qualification separate. A phase is complete only when its documented acceptance
gate has objective evidence.
