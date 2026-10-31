#!/bin/sh
# Offline NIX 0.3 ISO.
# NIX is not Linux. Linux 7.3.1 is a guest kernel source plus the built bzImage.
# The Debian packages install the compiler, the assembler, and the Linux build
# tools with no network. The Mac QEMU bundle launches the NIX kernel offline.
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
STAGE=$ROOT/build/iso-stage
DEBS=$ROOT/build/iso-debs
OUT=$ROOT/build/NIX-0.3-offline.iso
VERSION=$(tr -d ' \n' < VERSION)

need() {
  command -v "$1" >/dev/null || { echo "Missing $1" >&2; exit 1; }
}
need python3
need docker
need hdiutil

if [ ! -f build/linux/linux-7.3.1.tar.xz ]; then
  echo 'Missing build/linux/linux-7.3.1.tar.xz. Run scripts/guest-stack.sh fetch first.' >&2
  exit 1
fi
if [ ! -f build/linux/bzImage ]; then
  echo 'Missing build/linux/bzImage. Run scripts/guest-stack.sh build first.' >&2
  exit 1
fi
if [ ! -x build/core.elf ]; then
  echo 'Missing build/core.elf. Run scripts/build.sh first.' >&2
  exit 1
fi
if [ ! -f models/smollm2-135m-core-q8.bin ]; then
  echo 'Missing models/smollm2-135m-core-q8.bin.' >&2
  exit 1
fi

if ! docker info >/dev/null 2>&1; then
  echo 'Docker is not running. Start it, then retry.' >&2
  exit 1
fi

echo 'Downloading the Debian toolchain (compiler, assembler, kernel build tools).'
mkdir -p "$DEBS"
docker run --rm -v "$DEBS:/out" debian:stable bash -ceu '
  apt-get update
  apt-get install -y --download-only \
    build-essential clang lld nasm bc bison flex \
    libelf-dev libssl-dev cpio python3 pkg-config ca-certificates
  mkdir -p /out
  find /var/cache/apt/archives -name "*.deb" -exec cp -n {} /out/ \;
'

echo 'Staging the ISO tree.'
rm -rf "$STAGE"
mkdir -p "$STAGE/nix" "$STAGE/sources" "$STAGE/images" "$STAGE/offline-debs" \
  "$STAGE/host-qemu/bin" "$STAGE/host-qemu/lib" "$STAGE/host-qemu/pc-bios"

tar -C "$ROOT" -cf - \
  --exclude '__pycache__' --exclude '*.pyc' --exclude '.DS_Store' \
  --exclude 'models/.venv' \
  kernel user include scripts tests docs third_party \
  Makefile README.md VERSION index.html marketing.md Run-NIX.command models \
  | tar -C "$STAGE/nix" -xf -

cp build/linux/linux-7.3.1.tar.xz "$STAGE/sources/linux-7.3.1.tar.xz"
if [ -f build/linux/linux-7.3.1/.config ]; then
  cp build/linux/linux-7.3.1/.config "$STAGE/sources/linux-7.3.1-nix-guest.config"
fi
cp build/linux/bzImage "$STAGE/images/bzImage"
cp build/core.elf "$STAGE/images/core.elf"
cp "$DEBS"/*.deb "$STAGE/offline-debs/"

cat > "$STAGE/offline-debs/install-debian.sh" << EOF
#!/bin/sh
# Install the offline compiler, assembler, and Linux 7.3.1 build tools.
# Run this on Debian with the ISO mounted. No network is used.
set -eu
cd "\$(dirname "\$0")"
dpkg -i ./*.deb
echo 'Offline toolchain installed. clang, gcc, lld, nasm, make, bc, bison, and flex are available.'
EOF
chmod +x "$STAGE/offline-debs/install-debian.sh"

QEMU_BIN=$ROOT/build/tools/qemu-11.1.3/build/qemu-system-x86_64
if [ ! -x "$QEMU_BIN" ]; then
  echo 'Missing the local QEMU binary. NIX cannot be launched from the ISO without it.' >&2
  exit 1
fi
cp "$QEMU_BIN" "$STAGE/host-qemu/bin/qemu-system-x86_64"
cp -R "$ROOT/build/tools/qemu-11.1.3/pc-bios/." "$STAGE/host-qemu/pc-bios/"

python3 - "$QEMU_BIN" "$STAGE/host-qemu" << 'PY'
import os, shutil, subprocess, sys
binary, dest = sys.argv[1], sys.argv[2]
libdir = os.path.join(dest, "lib")
bindir = os.path.join(dest, "bin")
os.makedirs(libdir, exist_ok=True)
seen = []

def deps(path):
    out = subprocess.check_output(["otool", "-L", path], text=True)
    found = []
    for line in out.splitlines()[1:]:
        item = line.strip().split()[0]
        if item.startswith("/usr/lib/") or item.startswith("/System/"):
            continue
        if item.startswith("@"):
            continue
        found.append(item)
    return found

queue = [binary]
while queue:
    path = queue.pop()
    for dep in deps(path):
        if dep in seen:
            continue
        if not os.path.exists(dep):
            sys.exit(f"missing library {dep}")
        seen.append(dep)
        queue.append(dep)

copied = {}
for src in seen:
    name = os.path.basename(src)
    target = os.path.join(libdir, name)
    shutil.copy2(src, target)
    os.chmod(target, 0o755)
    copied[src] = "@rpath/" + name

def retarget(path):
    for src, new in copied.items():
        subprocess.check_call(["install_name_tool", "-change", src, new, path])
    ident = subprocess.check_output(["otool", "-D", path], text=True).splitlines()
    if len(ident) > 1 and ident[1].strip() and not ident[1].strip().startswith("/usr/lib") and not ident[1].strip().startswith("/System"):
        subprocess.check_call(["install_name_tool", "-id", "@rpath/" + os.path.basename(path), path])

retarget(os.path.join(bindir, "qemu-system-x86_64"))
subprocess.check_call(["install_name_tool", "-add_rpath", "@executable_path/../lib", os.path.join(bindir, "qemu-system-x86_64")])
for name in os.listdir(libdir):
    path = os.path.join(libdir, name)
    retarget(path)
    subprocess.check_call(["install_name_tool", "-add_rpath", "@loader_path", path])
print(f"Bundled {len(seen)} libraries")
PY

cat > "$STAGE/Run-NIX-offline.command" << EOF
#!/bin/sh
# Launch NIX ${VERSION} from this ISO. NIX stays the host. Linux is not started.
set -eu
ISO=\$(CDPATH= cd -- "\$(dirname "\$0")" && pwd)
DATA=\${NIX_DATA:-\$HOME/NIX-station}
mkdir -p "\$DATA"
if [ ! -f "\$DATA/memories.img" ]; then
  python3 "\$ISO/nix/scripts/memory.py" init "\$DATA/memories.img"
fi
python3 "\$ISO/nix/scripts/model_bundle.py" verify
exec "\$ISO/host-qemu/bin/qemu-system-x86_64" \\
  -L "\$ISO/host-qemu/pc-bios" \\
  -name "NIX / Core ${VERSION}" \\
  -machine pc,accel=tcg -cpu phenom -m 512M -vga std \\
  -kernel "\$ISO/images/core.elf" \\
  -initrd "\$ISO/nix/models/smollm2-135m-core-q8.bin" \\
  -drive "file=\$DATA/memories.img,format=raw,if=ide,index=0" \\
  -nic none
EOF
chmod +x "$STAGE/Run-NIX-offline.command"

cat > "$STAGE/README.txt" << EOF
NIX / Core ${VERSION} offline ISO
NIX is not Linux. NIX is the host. Linux 7.3.1 is the guest kernel source.

This disc has everything needed to run NIX on this Mac and to compile NIX
and Linux 7.3.1 on Debian without a network.

Run NIX
  Open Run-NIX-offline.command, or run it from a terminal.
  The first launch creates a blank Memories disk at ~/NIX-station/memories.img.
  You are dios. dios is root.
  The local model is SmolLM2-135M. It cannot confirm a plan and it cannot run tools.

What is on the disc
  images/core.elf
      The NIX kernel that QEMU boots.
  images/bzImage
      Linux 7.3.1, already compiled. It is a guest kernel. It is not NIX.
  sources/linux-7.3.1.tar.xz
      Vanilla Linux 7.3.1 source from kernel.org.
  sources/linux-7.3.1-nix-guest.config
      The guest config used to build images/bzImage, when that file was present.
  nix/
      NIX source: kernel, user pods, scripts, tests, and the resident assembler
      (nix/kernel/asm.c and nix/kernel/asm.h).
  nix/models/
      The SmolLM2-135M Q8 bundle NIX loads, plus its license and manifest.
  offline-debs/
      Debian packages and their dependencies: clang, lld, gcc, g++, binutils,
      make, nasm, bc, bison, flex, libelf-dev, libssl-dev, cpio, and Python 3.
  host-qemu/
      QEMU for this Mac, with the libraries it needs, so NIX can launch offline.

Install the compiler and assembler on Debian
  Mount this ISO and run:
    sh offline-debs/install-debian.sh
  That uses dpkg only. It does not contact the network.

Compile NIX, after the toolchain is installed
  cd nix
  ./scripts/build.sh
  The produced kernel is build/core.elf. NIX is still not Linux.

Compile Linux 7.3.1 from the included source
  tar -xJf sources/linux-7.3.1.tar.xz
  cd linux-7.3.1
  cp ../sources/linux-7.3.1-nix-guest.config .config   # when that config is on the disc
  make ARCH=x86_64 olddefconfig
  make -j ARCH=x86_64 bzImage
  The result is a guest kernel. Do not replace the NIX kernel with it.

Personal Memories are not on this disc.
EOF

(
  cd "$STAGE"
  find . -type f ! -name SHA256SUMS | sort | while IFS= read -r file; do
    shasum -a 256 "$file"
  done
) > "$STAGE/SHA256SUMS"

rm -f "$OUT"
echo 'Writing the ISO.'
hdiutil makehybrid -iso -joliet -default-volume-name "NIX-${VERSION}" -o "$OUT" "$STAGE"
ls -lh "$OUT"
echo "Wrote $OUT"
