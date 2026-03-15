# kpoke

Kernel memory inspection tool for embedded MIPS Linux. Zero dependencies — raw syscalls, no libc, ~3KB static binary. Fits in a `curl` one-liner.

Built for routers and IoT devices where there's no toolchain, no gdb, no `/proc/slabinfo`, and the filesystem is read-only squashfs. Upload it, run it, poke the kernel.

## Commands

```
kpoke read <addr> [count]           Read dwords from kernel memory
kpoke walk <list_head> <off> [max]  Walk circular doubly-linked list (Linux list_head)
kpoke slab <cache_addr>             Pretty-print struct kmem_cache (SLAB allocator)
kpoke pages <cache_addr>            Dump all slab objects with 2-dword fingerprints
kpoke write <addr> <val> [count]    Write dword(s) to kernel memory
kpoke flush <addr> <nbytes>         Flush I+D cache (required after writing code)
kpoke <addr> [count]                Legacy shorthand for read
```

All addresses accept kseg0 (`0x8xxxxxxx`) and auto-convert to physical for `/dev/mem`. Creates `/var/tmp/devmem` as a char device if `/dev/mem` isn't available.

## Build

Requires Docker (uses [dockcross](https://github.com/dockcross/dockcross) for MIPS cross-compilation):

```bash
make            # build with dockcross
make strip      # build + strip symbols (~2KB)
make local      # build with local mipsel-linux-gnu-gcc
```

## Deploy

```bash
# Serve the binary over HTTP (any method works)
cd dist && python3 -m http.server 8888

# On the target device:
curl -o /var/tmp/kpoke http://<your-ip>:8888/kpoke
chmod +x /var/tmp/kpoke
```

## Usage examples

```bash
# Read 16 dwords starting at 0x83dbe400
kpoke read 0x83dbe400 16

# Walk the SLAB cache linked list
kpoke walk 0x80344a44 0x44

# Inspect a kmem_cache struct (e.g., kmalloc-1024)
kpoke slab 0x83fb5e00

# Dump all objects in a slab cache with fingerprints
kpoke pages 0x83fb5e00

# Write a value to kernel memory
kpoke write 0x83dbe40c 0xdeadbeef

# Flush cache after code write
kpoke flush 0x83dbe400 256
```

## How it works

- Written in MIPS32 LE assembly with raw Linux syscalls (`__NR_open`, `__NR_read`, `__NR_write`, `__NR__llseek`, `__NR_mknod`)
- Accesses kernel memory through `/dev/mem` (or creates a char device at `/var/tmp/devmem` if the real one isn't available)
- Translates kseg0 virtual addresses to physical automatically
- Understands Linux 2.6.x SLAB allocator structs (`kmem_cache`, `kmem_list3`, `slab`)
- No libc, no dynamic linker, no dependencies — just a static ELF

## Target platforms

Currently MIPS32 little-endian only (MediaTek MT7628, MT7621, etc.). Works on Linux 2.6.36+ kernels with SLAB allocator.

## License

MIT
