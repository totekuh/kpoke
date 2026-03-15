# kpoke

Kernel memory inspection tool for embedded MIPS Linux. Zero dependencies — raw syscalls, no libc, static binaries under 12KB.

Built for routers and IoT devices where there's no toolchain, no gdb, no `/proc/slabinfo`, and the filesystem is read-only squashfs. Upload it, run it, poke the kernel.

## Two binaries

| Binary | Size | Description |
|--------|------|-------------|
| `kpoke` | ~10KB | Standalone CLI — one command per invocation |
| `kpoked` | ~12KB | TCP daemon — interactive session over the network |

## Commands

```
read <addr> [count]           Read dwords from kernel memory
walk <list_head> <off> [max]  Walk circular doubly-linked list (Linux list_head)
slab <cache_addr>             Pretty-print struct kmem_cache (SLAB allocator)
pages <cache_addr>            Dump all slab objects with 2-dword fingerprints
write <addr> <val> [count]    Write dword(s) to kernel memory
flush <addr> <nbytes>         Flush I+D cache (required after writing code)
<addr> [count]                Legacy shorthand for read
```

All addresses accept kseg0 (`0x8xxxxxxx`) and auto-convert to physical for `/dev/mem`. Creates `/var/tmp/devmem` as a char device if `/dev/mem` isn't available.

## Build

```bash
make            # build kpoke with dockcross (docker required)
make daemon     # build kpoked (TCP daemon)
make local      # build kpoke with local mipsel-linux-gnu-gcc
make strip      # build + strip symbols
make help       # show all targets
```

First time? Run `make setup` to pull the dockcross image, or `make setup-local` to install the apt cross-compiler.

## Deploy

```bash
# Serve the binaries over HTTP
./serve.sh

# On the target device:
curl -o /var/tmp/kpoke http://<your-ip>:8888/kpoke && chmod +x /var/tmp/kpoke
curl -o /var/tmp/kpoked http://<your-ip>:8888/kpoked && chmod +x /var/tmp/kpoked
```

## Usage — standalone CLI

```bash
kpoke read 0x83dbe400 16
kpoke walk 0x80344a44 0x44
kpoke slab 0x83fb5e00
kpoke pages 0x83fb5e00
kpoke write 0x83dbe40c 0xdeadbeef
kpoke flush 0x83dbe400 256
```

## Usage — TCP daemon

```bash
# On the target:
kpoked 4446 &

# From any machine on the network:
$ nc 192.168.240.1 4446
read 0x80000000 4
0x80000000: 0x3c1b8035
0x80000004: 0x401a4000
0x80000008: 0x8f7be000
0x8000000c: 0x001ad582
slab 0x83c809c0
  name:        bridge_fdb_cache
  buffer_size: 00000020 (32)
  ...
walk 0x80371258 0x44 3
#0: 0x83c809c0
#1: 0x83c80a20
#2: 0x83c80a80
total: 3
```

Multiple commands per connection. Interactive or scripted:

```bash
printf "read 0x80000000 4\nslab 0x83c809c0\n" | nc -w 2 192.168.240.1 4446
```

## How it works

- Written in MIPS32 LE assembly with raw Linux syscalls
- Accesses kernel memory through `/dev/mem` (or creates a char device at `/var/tmp/devmem` if the real one isn't available)
- Translates kseg0 virtual addresses to physical automatically
- Understands Linux 2.6.x SLAB allocator structs (`kmem_cache`, `kmem_list3`, `slab`)
- `kpoked` forks per connection, dup2()s the socket to stdout, reads commands line-by-line
- No libc, no dynamic linker, no dependencies — just static ELFs

## Target platforms

MIPS32 little-endian (MediaTek MT7628, MT7621, etc.). Linux 2.6.36+.

The `slab` and `pages` commands require the SLAB allocator with `NR_CPUS=1` (struct offsets are hardcoded). All other commands (`read`, `write`, `walk`, `flush`) work on any Linux kernel with `/dev/mem`.

## License

MIT
