# Project architecture

Read `blueprint/README.md`, `blueprint/architecture.md`, and `blueprint/phases.md`
before architecture or platform work. The two PDFs in `blueprint/` are the
owner-adopted architecture reference; preserve the originals unchanged.

Core is the operating system; NIX is its prompt-led host experience. Native apps
use Core services. Most non-native apps target separate Linux 7.3.1 guests;
Windows workloads require separate Windows guests.

Follow phases 0–10 in dependency order and prove prerequisite acceptance gates
before dependent implementation. Keep prototype behavior separate from completed
phases. Distinguish external QEMU hosting the development image, the current
direct SVM guest prototype, and future QEMU integration inside Core.

Preserve the owner's explicit requirements recorded in the blueprint, including
immutable base templates, named persistent instances, and paid Pioneer inference
through the Intent API. Payment and Cloudflare credentials stay on Cloudflare.

Preserve `build/memories.img`, existing user files, and unrelated working changes.
Use `README.md` for build/test commands and record the source revision, hardware,
rendering backend, test results, and unresolved dependencies with phase evidence.
