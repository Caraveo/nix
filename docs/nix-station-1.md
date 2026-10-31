# NIX Station 1

Phase 0 freezes one machine, the acceptance emulator, and the contracts Core 0.3 actually enforces. The physical PC is named here so later drivers have a target. This repository has not booted it. Every gate below was run on QEMU.

## Named machine

| Part | Choice |
| --- | --- |
| Name | NIX Station 1 |
| CPU | AMD Ryzen 7 7700, socket AM5. The generation includes Radeon Graphics, AMD-V (SVM with nested paging), and AMD-Vi. Those features were not read off a physical chip. |
| Board | ASUS TUF GAMING B650-PLUS WIFI, AMD B650, ATX. [Tech spec](https://www.asus.com/motherboards-components/motherboards/tuf-gaming/tuf-gaming-b650-plus-wifi/techspec/) |
| Memory | 32 GB as 2×16 GB DDR5-5600, non-ECC, unbuffered. The board lists 5600 as an overclocked rate. The highest rate it lists without that mark is 5200. |
| Graphics | No discrete GPU. The board's outputs are one DisplayPort 1.4 (max 8K@60 as specified) and one HDMI 2.1 (4K@60 as specified). NIX does not drive them yet. |
| Storage | Samsung 990 PRO 1 TB NVMe, in M.2_1. On a Ryzen 7000 that slot is PCIe 5.0 x4 and accepts this drive. |
| Network | The board lists one Realtek 2.5Gb Ethernet port. It does not publish a Realtek part number on that page. Wi-Fi 6 (802.11ax, 2.4/5 GHz) and Bluetooth 5.2 are present and out of the first driver set. |
| Audio | The board lists a Realtek 7.1 HD codec and does not name the codec part. Audio is deferred. |
| Firmware plan | The board ships a 256 Mb UEFI AMI BIOS. When this PC is used, AMD-Vi stays on and Secure Boot stays off until a signed loader exists. That setup has not been applied. |

An Intel board was not chosen. QEMU 11.1.3 TCG does not execute VMX. It does execute AMD SVM and nested paging. The CPU model is `phenom`, which advertises both. The acceptance command is:

```text
-machine pc,accel=tcg -cpu phenom -m 512M -vga std -nic none
```

The disk is IDE. Input is PS/2. `scripts/run.sh` and the tests use this machine. Do not pass `-cpu qemu64,vmx=on`. TCG warns that it cannot provide that bit.

## Threat model

There is one human principal, `dios`. `dios` is root. Ghost `whoami` prints `dios`. A plan runs only after dios types `y` or `yes`. The model cannot confirm. Its answers are not commands.

The acceptance VM has no NIC. This QEMU build has no user-mode network backend, so the launcher does not attach one. `fetch` answers `Network denied. The catalog is local.` The catalog in this milestone is the recipe `count-words`, carried inside the build guest and checked for the ELF magic before install. There is no general compiler and no opt-in catalog download.

Native pods are ring 3. A loadable segment that is both writable and executable is rejected. Code pages are executable. Data and the stack are not executable. The pod window is the 2 MiB at `0x2000000`. The top 32 KiB is stack, so a 4 KiB buffer fits. A fault in a pod prints `Pod faulted. NIX is still running.` and returns to the shell. The kernel does not restart that pod.

The build guest is a real AMD SVM guest with nested paging, not a ring-3 process pretending to be a hypervisor. It receives the recipe name in a mailbox at guest physical 0. It does not receive Memories or any other host object. Its 2 MiB guest map is one writable and executable nested page. Nested paging is the isolation boundary. Do not describe that guest as W^X inside.

A Memory grant is RAM-only. It is cleared on reboot. dios grants one book to the native `counter` pod. The guest that produced the pod does not receive the book. `run counter` without a grant prints `Memory grant: denied.` and counts what dios types instead.

Reuse comes before a new guest. `build count-words` on an installed pod does not enter the hypervisor. `rebuild count-words` does. Install keeps the previous bytes. `rollback counter` restores them. A failed install writes the previous record back.

Scheduling is cooperative. Interrupts stay masked. One pod runs until it calls back into the kernel. The local model still runs in the kernel for this milestone and has no tool path.

Memory snapshots use a checksum so a torn bank can be rejected. That checksum is not a signature and not encryption. The pod record lives at LBA 2048, past the two Memory banks.

## Boot plan

Today, QEMU loads `build/core.elf` with Multiboot v1. The ELF32 stub enables paging, sets EFER long mode, and calls the ELF64 kernel at physical 2 MiB. The stub identity-maps the first 4 GiB with 2 MiB pages. The top gigabyte is marked uncached for the framebuffer. This is not a UEFI boot and not a boot of Station 1.

The later Station 1 loader, not started here, is UEFI with AMD-Vi on and Secure Boot off. The first device drivers, when that boot exists, are the CPU M.2 NVMe slot and the onboard Realtek 2.5Gb Ethernet. Wi-Fi, Bluetooth, and audio stay out of that set.

## Contracts that boot

Syscall numbers live in `include/nix.h`. The kernel returns in `rax` and preserves every other general register except `rcx` and `r11`.

The build mailbox is `struct NixMail`: a 64-byte recipe, a status, a length, and up to 16384 bytes of ELF. It sits at guest physical 0 and ends before the guest stack at `0x7000`. The guest calls `vmmcall` when it is done. `probe-fault` stores to virtual `0x400000`, which the guest maps and nested paging does not. The host accepts that probe only when the nested fault's guest physical address falls in `0x400000`–`0x600000`.

The on-disk pod record is `struct PodDisk` at LBA 2048. Book layout is unchanged: four books, 4096-byte bodies, two banks of 34 sectors. A version counter sits in the old reserved pad. `scripts/memory.py` still reads existing disks.

## What phase 4 showed

On QEMU phenom, `tests/phase4.py` does all of the following:

- dios can cancel a plan, and an unknown recipe never enters the guest.
- `probe-fault` stops on a nested page fault. The manuscript text is not on the serial log.
- `count-words` installs version 1, prints a manifest, and a second build reuses the pod without entering the hypervisor.
- `run counter` is denied the book, then counts the line dios types.
- A pod that executes `ud2` faults and the shell still accepts `help`.
- Rebuild then rollback returns the previous pod.
- A confirmed grant lets `counter` count the book. The build guest's output does not contain the book. After reboot the installed pod is reused from disk, and the book is still the saved text.

`tests/smoke.py` and `tests/power.py` still pass on the same kernel, including Author save, restart, and shutdown.

## Not claimed

Station 1 has not been powered on by this tree. There is no NVMe, Ethernet, Wi-Fi, audio, or GPU driver. There is no preemption, no UEFI, no Rust core, and no OpenCode guest. The catalog cannot compile an arbitrary program. The model cannot run a plan.
