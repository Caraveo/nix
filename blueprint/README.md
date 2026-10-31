# NIX / Core architecture blueprint

These documents are the architecture reference for this project, adopted by the
project owner on 2026-10-31. The PDFs are preserved byte for byte.

- [Executive architecture](NIX_Executive_Architecture.pdf): system boundaries,
  per-application virtualization, graphics, and build direction.
- [Architecture and Codex phases](NIX_Architecture_Codex_Phases.pdf): detailed
  architecture, graphics contracts, and phases 0–10 with acceptance gates.
- [Repository architecture contracts](architecture.md): searchable implementation
  guidance derived from the PDFs and the owner's explicit requirements.
- [Phase plan and evidence](phases.md): repository mapping, verification, current
  limitations, and dependency order.

Core is the operating system. NIX is its prompt-led host experience. Most
non-native applications receive separate Linux 7.3.1 guests; native applications
use Core services directly, and Windows workloads require Windows guests.

Follow the phase dependencies and prove each acceptance gate before advancing
dependent work. Existing prototypes are evidence of specific behavior, not proof
that an entire phase is complete. The repository's older web mockups and design
notes are supporting material; these blueprints guide the architecture.

The owner's explicit paid Pioneer requirement remains in force: Pioneer uses
only the NIX Intent API and server-managed credits. Payment credentials and
Cloudflare credentials remain on Cloudflare. A client's own paid Intent token is
distinct from those server credentials. This is separate from the blueprint's
local AI service for the NIX prompt and launch-control experience.

The current external QEMU development VM, direct SVM Linux-guest prototype, and
future QEMU integration inside Core must be described separately.
