/*
 * common.inc — shared commands, helpers, and data for kpoke/kpoked
 *
 * Expects the includer to set up:
 *   $s3 = argc
 *   $s6 = argv base pointer (array of char* pointers)
 * Then jump/fall into the dispatch or command labels.
 */

/* ============================================================
 * CMD: read <addr> [count]
 * ============================================================ */
cmd_read_named:
    /* argv[2] = addr, argv[3] = count */
    slti    $v0, $s3, 3
    bnez    $v0, usage
    nop
    lw      $a0, 8($s6)         /* argv[2] */
    jal     parse_hex
    nop
    move    $s0, $v0

    li      $s4, 4              /* default count */
    slti    $v0, $s3, 4
    bnez    $v0, .read_go
    nop
    lw      $a0, 12($s6)        /* argv[3] */
    jal     parse_dec
    nop
    move    $s4, $v0
    j       .read_go
    nop

.read_go:
    jal     devmem_open
    nop

    move    $s2, $s0            /* s2 = current addr (kernel virt) */
    move    $s5, $zero          /* s5 = counter */

.read_loop:
    beq     $s5, $s4, exit_ok
    nop

    move    $a0, $s2
    jal     devmem_read4
    nop
    move    $s7, $v0            /* s7 = value */

    /* Print "0x" + addr + ": 0x" + value + "\n" */
    move    $a0, $s2
    move    $a1, $s7
    jal     print_addr_val
    nop

    addiu   $s2, $s2, 4
    addiu   $s5, $s5, 1
    j       .read_loop
    nop

/* ============================================================
 * CMD: walk <list_head_addr> <entry_offset> [max]
 *
 * Walks a circular doubly-linked list (Linux list_head).
 * Prints base address of each containing struct.
 * entry_offset = offset of list_head within the struct.
 * Stops when we loop back to head or hit max.
 * ============================================================ */
cmd_walk:
    slti    $v0, $s3, 4         /* need at least: walk addr offset */
    bnez    $v0, usage_walk
    nop

    /* Parse list_head address */
    lw      $a0, 8($s6)         /* argv[2] */
    jal     parse_hex
    nop
    move    $s0, $v0            /* s0 = list_head addr (head of list) */

    /* Parse entry_offset */
    lw      $a0, 12($s6)        /* argv[3] */
    jal     parse_hex
    nop
    move    $s4, $v0            /* s4 = entry_offset */

    /* Parse max (default 128) */
    li      $s5, 128
    slti    $v0, $s3, 5
    bnez    $v0, .walk_go
    nop
    lw      $a0, 16($s6)        /* argv[4] */
    jal     parse_dec
    nop
    move    $s5, $v0            /* s5 = max entries */

.walk_go:
    jal     devmem_open
    nop

    /* Read head->next to get first entry */
    move    $a0, $s0
    jal     devmem_read4
    nop
    move    $s2, $v0            /* s2 = current node (points to next field) */

    move    $s7, $zero          /* s7 = counter */

.walk_loop:
    /* Check: current == head? (looped back) */
    beq     $s2, $s0, .walk_done
    nop

    /* Check: counter >= max? */
    slt     $v0, $s7, $s5
    beqz    $v0, .walk_done
    nop

    /* Print "#N: 0x<struct_base>\n" */
    PRINT walk_prefix
    move    $a0, $s7
    jal     print_dec
    nop
    PRINT walk_sep
    PRINT hex_prefix
    subu    $a0, $s2, $s4
    jal     print_hex32
    nop
    PRINT newline

    /* Follow: read current->next */
    move    $a0, $s2
    jal     devmem_read4
    nop
    move    $s2, $v0            /* s2 = next node */

    addiu   $s7, $s7, 1
    j       .walk_loop
    nop

.walk_done:
    PRINT walk_total
    move    $a0, $s7
    jal     print_dec
    nop
    PRINT newline

    j       exit_ok
    nop

/* ============================================================
 * CMD: slab <cache_addr>
 *
 * Pretty-prints struct kmem_cache fields.
 * Layout assumes NR_CPUS=1 (single array_cache pointer at +0x00).
 * Validates by checking name pointer looks like a kseg0 string.
 *
 * struct kmem_cache layout (NR_CPUS=1, Linux 2.6.36 SLAB):
 *   +0x00  array_cache *array[1]
 *   +0x04  batchcount
 *   +0x08  limit
 *   +0x0C  shared
 *   +0x10  buffer_size
 *   +0x14  reciprocal_buffer_size
 *   +0x18  flags
 *   +0x1C  num (objects per slab)
 *   +0x20  gfporder (pages = 2^order)
 *   +0x24  gfpflags
 *   +0x28  colour
 *   +0x2C  colour_off
 *   +0x30  slabp_cache
 *   +0x34  slab_size
 *   +0x38  dflags
 *   +0x3C  ctor
 *   +0x40  name (char *)
 *   +0x44  next (list_head, 8 bytes)
 *   +0x4C  nodelists[0] -> struct kmem_list3
 * ============================================================ */
cmd_slab:
    slti    $v0, $s3, 3
    bnez    $v0, usage
    nop

    /* Parse cache address */
    lw      $a0, 8($s6)         /* argv[2] */
    jal     parse_hex
    nop
    move    $s0, $v0            /* s0 = cache base addr */

    jal     devmem_open
    nop

    /* --- Validate: check name pointer at +0x40 --- */
    addiu   $a0, $s0, 0x40
    jal     devmem_read4
    nop
    move    $s2, $v0            /* s2 = name pointer */

    /* Check pointer is in kseg0 (0x80000000-0x9FFFFFFF) */
    li      $t0, 0x80000000
    sltu    $t1, $s2, $t0
    bnez    $t1, .slab_bad_layout
    nop
    li      $t0, 0xa0000000
    sltu    $t1, $s2, $t0
    beqz    $t1, .slab_bad_layout
    nop

    /* Check first byte of name is printable ASCII (0x20-0x7E) */
    move    $a0, $s2
    jal     devmem_read4
    nop
    andi    $t0, $v0, 0xFF      /* first byte (little-endian) */
    slti    $t1, $t0, 0x20
    bnez    $t1, .slab_bad_layout
    nop
    slti    $t1, $t0, 0x7F
    beqz    $t1, .slab_bad_layout
    nop
    j       .slab_ok
    nop

.slab_bad_layout:
    EPRINT slab_warn_msg
    j       exit_ok
    nop

.slab_ok:
    /* --- Print name --- */
    PRINT slab_f_name

    /* Read and print name string (up to 32 chars) */
    move    $s4, $s2            /* s4 = name ptr */
    li      $s5, 0              /* char counter */
.slab_name_loop:
    slti    $v0, $s5, 32
    beqz    $v0, .slab_name_done
    nop
    move    $a0, $s4
    jal     devmem_read4
    nop
    /* Print up to 4 chars from this dword */
    addiu   $sp, $sp, -8
    sw      $v0, 0($sp)
    li      $t4, 0              /* byte index within dword */
.slab_name_byte:
    slti    $v0, $t4, 4
    beqz    $v0, .slab_name_next
    nop
    addu    $t5, $sp, $t4
    lbu     $t0, 0($t5)         /* read byte at index within dword */
    beqz    $t0, .slab_name_end_found
    nop
    /* Write one char */
    sb      $t0, 4($sp)
    li      $a0, 1
    addiu   $a1, $sp, 4
    li      $a2, 1
    li      $v0, __NR_write
    syscall
    addiu   $t4, $t4, 1
    addiu   $s5, $s5, 1
    j       .slab_name_byte
    nop
.slab_name_next:
    addiu   $sp, $sp, 8
    addiu   $s4, $s4, 4
    j       .slab_name_loop
    nop
.slab_name_end_found:
    addiu   $sp, $sp, 8
.slab_name_done:
    PRINT newline

    /* --- Print numeric fields --- */
    SLAB_FIELD slab_f_bufsz,  0x10
    SLAB_FIELD slab_f_num,    0x1C
    SLAB_FIELD slab_f_order,  0x20
    SLAB_FIELD slab_f_batch,  0x04
    SLAB_FIELD slab_f_limit,  0x08
    SLAB_FIELD slab_f_flags,  0x18
    SLAB_FIELD slab_f_colour, 0x28
    SLAB_FIELD slab_f_coloff, 0x2C
    SLAB_FIELD slab_f_slabsz, 0x34
    SLAB_FIELD slab_f_node,   0x4C

    j       exit_ok
    nop

/* slab_print_field: print "  label: value (decimal)\n"
 * $a0 = field kernel address
 * $a1 = label string
 * $a2 = label length
 * Clobbers everything except s0, s1, s3, s6 */
slab_print_field:
    addiu   $sp, $sp, -16
    sw      $ra, 12($sp)
    sw      $a0, 8($sp)         /* save field addr */

    /* Print label */
    li      $a0, 1
    li      $v0, __NR_write
    syscall

    /* Read field value */
    lw      $a0, 8($sp)
    jal     devmem_read4
    nop
    sw      $v0, 4($sp)         /* save value */

    /* Print hex value */
    move    $a0, $v0
    jal     print_hex32
    nop

    PRINT slab_paren_open
    lw      $a0, 4($sp)
    jal     print_dec
    nop
    PRINT slab_paren_close

    lw      $ra, 12($sp)
    addiu   $sp, $sp, 16
    jr      $ra
    nop

/* ============================================================
 * CMD: pages <cache_addr>
 *
 * Walks slabs_full and slabs_partial for a kmem_cache.
 * For each slab, prints s_mem and first 2 dwords of each object.
 *
 * struct kmem_list3 layout:
 *   +0x00  slabs_partial (list_head)
 *   +0x08  slabs_full (list_head)
 *   +0x10  slabs_free (list_head)
 *   +0x18  free_objects
 *
 * struct slab layout (off-slab):
 *   +0x00  list (list_head) — offset 0, so slab_base == list_entry
 *   +0x08  colouroff
 *   +0x0C  s_mem (void *) — first object address
 *   +0x10  inuse
 * ============================================================ */
cmd_pages:
    slti    $v0, $s3, 3
    bnez    $v0, usage
    nop

    /* Allocate frame — buffer_size and num must survive subroutine calls */
    addiu   $sp, $sp, -24
    sw      $ra, 20($sp)

    lw      $a0, 8($s6)         /* argv[2] = cache addr */
    jal     parse_hex
    nop
    move    $s0, $v0            /* s0 = cache base */

    jal     devmem_open
    nop

    /* Read buffer_size (+0x10) */
    addiu   $a0, $s0, 0x10
    jal     devmem_read4
    nop
    sw      $v0, 0($sp)         /* save buffer_size in our frame */

    /* Read num (+0x1C) */
    addiu   $a0, $s0, 0x1C
    jal     devmem_read4
    nop
    sw      $v0, 4($sp)         /* save num in our frame */

    /* Read nodelists[0] (+0x4C) */
    addiu   $a0, $s0, 0x4C
    jal     devmem_read4
    nop
    move    $s0, $v0            /* s0 = nodelists addr */

    PRINT pages_hdr_full

    /* Walk slabs_full: list head at nodelists+0x08 */
    addiu   $s2, $s0, 0x08      /* s2 = &slabs_full (list head addr) */
    move    $s7, $zero          /* s7 = slab counter */

    /* Read slabs_full.next */
    move    $a0, $s2
    jal     devmem_read4
    nop
    move    $s4, $v0            /* s4 = current slab */

.pages_full_loop:
    beq     $s4, $s2, .pages_full_done
    nop

    move    $a0, $s4            /* slab addr */
    lw      $a1, 0($sp)         /* buffer_size from our frame */
    lw      $a2, 4($sp)         /* num from our frame */
    move    $a3, $s7            /* slab index */
    jal     pages_print_slab
    nop

    /* Follow list.next */
    move    $a0, $s4
    jal     devmem_read4
    nop
    move    $s4, $v0

    addiu   $s7, $s7, 1

    slti    $v0, $s7, 256
    bnez    $v0, .pages_full_loop
    nop

.pages_full_done:
    PRINT pages_hdr_part

    /* Walk slabs_partial: list head at nodelists+0x00 */
    move    $s2, $s0            /* s2 = &slabs_partial */

    move    $a0, $s2
    jal     devmem_read4
    nop
    move    $s4, $v0

.pages_part_loop:
    beq     $s4, $s2, .pages_part_done
    nop

    move    $a0, $s4
    lw      $a1, 0($sp)
    lw      $a2, 4($sp)
    move    $a3, $s7
    jal     pages_print_slab
    nop

    move    $a0, $s4
    jal     devmem_read4
    nop
    move    $s4, $v0

    addiu   $s7, $s7, 1

    slti    $v0, $s7, 256
    bnez    $v0, .pages_part_loop
    nop

.pages_part_done:
    PRINT walk_total
    move    $a0, $s7
    jal     print_dec
    nop
    PRINT pages_slabs_suffix

    lw      $ra, 20($sp)
    addiu   $sp, $sp, 24
    j       exit_ok
    nop

/* pages_print_slab: print one slab's info and object fingerprints
 * $a0 = slab addr
 * $a1 = buffer_size
 * $a2 = num (objects per slab)
 * $a3 = slab index
 */
pages_print_slab:
    addiu   $sp, $sp, -40
    sw      $ra, 36($sp)
    sw      $a0, 32($sp)        /* slab addr */
    sw      $a1, 28($sp)        /* buffer_size */
    sw      $a2, 24($sp)        /* num */
    sw      $a3, 20($sp)        /* index */

    /* Read s_mem at slab+0x0C */
    addiu   $a0, $a0, 0x0C
    jal     devmem_read4
    nop
    sw      $v0, 16($sp)        /* save s_mem */

    /* Read inuse at slab+0x10 */
    lw      $a0, 32($sp)
    addiu   $a0, $a0, 0x10
    jal     devmem_read4
    nop
    sw      $v0, 12($sp)        /* save inuse */

    /* Print "slab N @ 0x<s_mem> inuse=<inuse>\n" */
    PRINT pages_slab_pre
    lw      $a0, 20($sp)        /* index */
    jal     print_dec
    nop
    PRINT pages_at
    PRINT hex_prefix
    lw      $a0, 16($sp)        /* s_mem */
    jal     print_hex32
    nop
    PRINT pages_inuse
    lw      $a0, 12($sp)        /* inuse */
    jal     print_dec
    nop
    PRINT newline

    /* For each object: print "  [i] 0x<addr> <dw0> <dw1>" */
    li      $t6, 0              /* object index */
    lw      $t7, 16($sp)        /* s_mem (current obj addr) */

.pages_obj_loop:
    lw      $t0, 24($sp)        /* num */
    slt     $v0, $t6, $t0
    beqz    $v0, .pages_obj_done
    nop

    /* Save t6, t7 — they'll be clobbered by subroutine calls */
    sw      $t6, 8($sp)
    sw      $t7, 4($sp)

    /* Print "  [N] 0x<addr> <dw0> <dw1>\n" */
    PRINT pages_obj_pre
    lw      $a0, 8($sp)         /* t6 */
    jal     print_dec
    nop
    PRINT pages_obj_mid
    lw      $a0, 4($sp)         /* t7 = obj addr */
    jal     print_hex32
    nop
    PRINT pages_space
    lw      $a0, 4($sp)         /* obj addr */
    jal     devmem_read4
    nop
    move    $a0, $v0
    jal     print_hex32
    nop
    PRINT pages_space
    lw      $a0, 4($sp)         /* obj addr */
    addiu   $a0, $a0, 4
    jal     devmem_read4
    nop
    move    $a0, $v0
    jal     print_hex32
    nop
    PRINT newline

    /* Next object: addr += buffer_size */
    lw      $t6, 8($sp)
    lw      $t7, 4($sp)
    addiu   $t6, $t6, 1
    lw      $t0, 28($sp)        /* buffer_size */
    addu    $t7, $t7, $t0
    j       .pages_obj_loop
    nop

.pages_obj_done:
    lw      $ra, 36($sp)
    addiu   $sp, $sp, 40
    jr      $ra
    nop

/* ============================================================
 * CMD: write <addr> <hex_val> [count]
 *
 * Write dword(s) to kernel memory.
 * Usage: kpoke write 0x83fff000 0xdeadbeef
 *        kpoke write 0x83fff000 0xdeadbeef 4  (writes 4 copies)
 * ============================================================ */
cmd_write:
    slti    $v0, $s3, 4         /* need at least: write addr val */
    bnez    $v0, usage
    nop

    /* Parse address */
    lw      $a0, 8($s6)         /* argv[2] = addr */
    jal     parse_hex
    nop
    move    $s0, $v0            /* s0 = target addr */

    /* Parse value */
    lw      $a0, 12($s6)        /* argv[3] = value */
    jal     parse_hex
    nop
    move    $s4, $v0            /* s4 = value to write */

    /* Parse count (default 1) */
    li      $s5, 1
    slti    $v0, $s3, 5
    bnez    $v0, .write_go
    nop
    lw      $a0, 16($s6)        /* argv[4] = count */
    jal     parse_dec
    nop
    move    $s5, $v0

.write_go:
    jal     devmem_open
    nop

    move    $s2, $s0            /* s2 = current addr */
    move    $s7, $zero          /* s7 = counter */

.write_loop:
    beq     $s7, $s5, .write_done
    nop

    /* Write one dword */
    move    $a0, $s2
    move    $a1, $s4
    jal     devmem_write4
    nop

    /* Print confirmation */
    move    $a0, $s2
    move    $a1, $s4
    jal     print_addr_val
    nop

    addiu   $s2, $s2, 4
    addiu   $s7, $s7, 1
    j       .write_loop
    nop

.write_done:
    j       exit_ok
    nop

/* ============================================================
 * CMD: flush <addr> <nbytes>
 * Flush I-cache + D-cache for a memory range.
 * Required after writing code to kernel memory.
 * cacheflush(addr, nbytes, BCACHE=3) — syscall 4147
 * ============================================================ */
cmd_flush:
    slti    $v0, $s3, 4
    bnez    $v0, usage
    nop

    lw      $a0, 8($s6)         /* argv[2] = addr */
    jal     parse_hex
    nop
    move    $a0, $v0            /* a0 = addr */

    lw      $t0, 12($s6)        /* argv[3] = nbytes */
    move    $s0, $a0            /* save addr */
    move    $a0, $t0            /* parse nbytes as decimal */
    jal     parse_dec
    nop
    move    $a1, $v0            /* a1 = nbytes */
    move    $a0, $s0            /* a0 = addr */

    li      $a2, 3              /* BCACHE = flush both I and D */
    li      $v0, 4147           /* __NR_cacheflush */
    syscall

    bnez    $a3, .flush_err
    nop

    PRINT flush_ok_msg
    j       exit_ok
    nop

.flush_err:
    EPRINT flush_err_msg
    j       exit_err
    nop

/* ============================================================
 * CMD: find (TODO)
 * ============================================================ */
cmd_find:
cmd_dump:
cmd_string:
    EPRINT todo_msg
    j       exit_err
    nop

/* ============================================================
 * SHARED: str_match(a, b)
 * Compare null-terminated strings.
 * $a0 = string a, $a1 = string b
 * Returns $v0 = 0 if match, 1 if mismatch.
 * ============================================================ */
str_match:
    lbu     $t0, 0($a0)
    lbu     $t1, 0($a1)
    bne     $t0, $t1, .sm_no
    nop
    beqz    $t0, .sm_yes
    nop
    addiu   $a0, $a0, 1
    addiu   $a1, $a1, 1
    j       str_match
    nop
.sm_yes:
    jr      $ra
    move    $v0, $zero
.sm_no:
    jr      $ra
    li      $v0, 1

/* ============================================================
 * SHARED: devmem_open
 * Creates /var/tmp/devmem if needed, opens it.
 * Returns fd in $s1.
 * ============================================================ */
devmem_open:
    addiu   $sp, $sp, -8
    sw      $ra, 4($sp)

    /* mknod (ignore error) */
    la      $a0, devpath
    li      $a1, 0x21B6         /* S_IFCHR | 0666 */
    li      $a2, 0x0101         /* makedev(1,1) = /dev/mem */
    li      $v0, __NR_mknod
    syscall

    /* open */
    la      $a0, devpath
    li      $a1, O_RDWR
    li      $a2, 0
    li      $v0, __NR_open
    syscall
    bltz    $v0, err_open
    nop
    move    $s1, $v0

    lw      $ra, 4($sp)
    addiu   $sp, $sp, 8
    jr      $ra
    nop

/* ============================================================
 * SHARED: devmem_read4(addr)
 * Reads 4 bytes from kernel virtual address via /dev/mem.
 * $a0 = kernel virtual address (kseg0/kseg1 auto-converted)
 * Returns value in $v0.
 * Clobbers: $a0-$a3, $t0-$t2, $v0-$v1
 * ============================================================ */
devmem_read4:
    addiu   $sp, $sp, -32
    sw      $ra, 28($sp)

    /* kseg0/kseg1 to physical: strip top 3 bits */
    li      $t0, 0x80000000
    sltu    $t1, $a0, $t0
    bnez    $t1, .dr4_noc
    move    $t2, $a0            /* t2 = original addr (delay slot) */
    li      $t0, 0x1fffffff
    and     $t2, $a0, $t0       /* t2 = physical addr */
.dr4_noc:

    /* _llseek(fd, 0, phys_addr, &result, SEEK_SET) */
    move    $a0, $s1
    li      $a1, 0
    move    $a2, $t2
    addiu   $a3, $sp, 0         /* &result on stack */
    sw      $zero, 16($sp)      /* whence = SEEK_SET */
    li      $v0, __NR__llseek
    syscall
    bnez    $a3, err_seek
    nop

    /* read(fd, buf, 4) */
    move    $a0, $s1
    addiu   $a1, $sp, 8         /* buf */
    li      $a2, 4
    li      $v0, __NR_read
    syscall

    lw      $v0, 8($sp)         /* return value */
    lw      $ra, 28($sp)
    addiu   $sp, $sp, 32
    jr      $ra
    nop

/* ============================================================
 * SHARED: devmem_write4(addr, val)
 * Writes 4 bytes to kernel virtual address via /dev/mem.
 * $a0 = kernel virtual address (kseg0/kseg1 auto-converted)
 * $a1 = value to write
 * ============================================================ */
devmem_write4:
    addiu   $sp, $sp, -32
    sw      $ra, 28($sp)

    /* kseg0/kseg1 to physical: strip top 3 bits */
    li      $t0, 0x80000000
    sltu    $t1, $a0, $t0
    bnez    $t1, .dw4_noc
    move    $t2, $a0            /* t2 = original addr (delay slot) */
    li      $t0, 0x1fffffff
    and     $t2, $a0, $t0       /* t2 = physical addr */
.dw4_noc:
    /* Save value to write */
    sw      $a1, 8($sp)

    /* _llseek(fd, 0, phys_addr, &result, SEEK_SET) */
    move    $a0, $s1
    li      $a1, 0
    move    $a2, $t2
    addiu   $a3, $sp, 0
    sw      $zero, 16($sp)
    li      $v0, __NR__llseek
    syscall
    bnez    $a3, err_seek
    nop

    /* write(fd, &val, 4) */
    move    $a0, $s1
    addiu   $a1, $sp, 8         /* &val */
    li      $a2, 4
    li      $v0, __NR_write
    syscall

    lw      $ra, 28($sp)
    addiu   $sp, $sp, 32
    jr      $ra
    nop

/* ============================================================
 * SHARED: print_addr_val(addr, val)
 * Prints "0x<addr>: 0x<val>\n"
 * $a0 = addr, $a1 = val
 * ============================================================ */
print_addr_val:
    addiu   $sp, $sp, -24
    sw      $ra, 20($sp)
    sw      $s7, 16($sp)        /* preserve s7 for caller */
    sw      $a1, 8($sp)         /* save val */

    move    $s7, $a0            /* save addr before macro clobbers $a0 */
    PRINT hex_prefix
    move    $a0, $s7
    jal     print_hex32
    nop
    PRINT sep
    lw      $a0, 8($sp)
    jal     print_hex32
    nop
    PRINT newline

    lw      $s7, 16($sp)        /* restore s7 */
    lw      $ra, 20($sp)
    addiu   $sp, $sp, 24
    jr      $ra
    nop

/* ============================================================
 * SHARED: print_hex32(val)
 * Prints 32-bit value as 8 hex digits.
 * $a0 = value
 * ============================================================ */
print_hex32:
    addiu   $sp, $sp, -24
    sw      $ra, 20($sp)
    addiu   $t3, $sp, 0
    li      $t4, 8
.ph32loop:
    srl     $t0, $a0, 28
    slti    $t1, $t0, 10
    bnez    $t1, .ph32dec
    nop
    addiu   $t0, $t0, ('a' - 10)
    j       .ph32store
    nop
.ph32dec:
    addiu   $t0, $t0, '0'
.ph32store:
    sb      $t0, 0($t3)
    addiu   $t3, $t3, 1
    sll     $a0, $a0, 4
    addiu   $t4, $t4, -1
    bnez    $t4, .ph32loop
    nop
    li      $a0, 1
    addiu   $a1, $sp, 0
    li      $a2, 8
    li      $v0, __NR_write
    syscall
    lw      $ra, 20($sp)
    addiu   $sp, $sp, 24
    jr      $ra
    nop

/* ============================================================
 * SHARED: print_dec(val)
 * Prints unsigned 32-bit value as decimal.
 * $a0 = value
 * ============================================================ */
print_dec:
    addiu   $sp, $sp, -24
    sw      $ra, 20($sp)
    addiu   $t3, $sp, 9         /* end of buffer (build right-to-left) */
    move    $t4, $a0
    li      $t5, 10

    /* Handle 0 */
    bnez    $t4, .pd_loop
    nop
    li      $t0, '0'
    sb      $t0, 0($t3)
    addiu   $t3, $t3, -1
    j       .pd_print
    nop

.pd_loop:
    beqz    $t4, .pd_print
    nop
    divu    $zero, $t4, $t5
    mfhi    $t0                 /* remainder */
    mflo    $t4                 /* quotient */
    addiu   $t0, $t0, '0'
    sb      $t0, 0($t3)
    addiu   $t3, $t3, -1
    j       .pd_loop
    nop

.pd_print:
    addiu   $t3, $t3, 1         /* start of string */
    addiu   $t6, $sp, 10
    subu    $a2, $t6, $t3       /* length */
    move    $a1, $t3
    li      $a0, 1
    li      $v0, __NR_write
    syscall

    lw      $ra, 20($sp)
    addiu   $sp, $sp, 24
    jr      $ra
    nop

/* ============================================================
 * SHARED: parse_hex, parse_dec
 * ============================================================ */
parse_hex:
    li      $v0, 0
    lbu     $t0, 0($a0)
    bne     $t0, '0', .phloop
    nop
    lbu     $t0, 1($a0)
    li      $t1, 'x'
    beq     $t0, $t1, .skip2
    li      $t1, 'X'
    bne     $t0, $t1, .phloop
    nop
.skip2:
    addiu   $a0, $a0, 2
.phloop:
    lbu     $t0, 0($a0)
    beqz    $t0, .phdone
    nop
    sll     $v0, $v0, 4
    addiu   $t1, $t0, -'0'
    slti    $t2, $t1, 10
    bnez    $t2, .phdig
    nop
    addiu   $t1, $t0, -'a'
    slti    $t2, $t1, 6
    bnez    $t2, .phlc
    nop
    addiu   $t1, $t0, -'A'
    slti    $t2, $t1, 6
    beqz    $t2, .phdone
    nop
    addiu   $t1, $t1, 10
    j       .phdig
    nop
.phlc:
    addiu   $t1, $t1, 10
.phdig:
    or      $v0, $v0, $t1
    addiu   $a0, $a0, 1
    j       .phloop
    nop
.phdone:
    jr      $ra
    nop

parse_dec:
    li      $v0, 0
.pdloop:
    lbu     $t0, 0($a0)
    beqz    $t0, .pddone
    addiu   $t1, $t0, -'0'
    bltz    $t1, .pddone
    nop
    slti    $t2, $t1, 10
    beqz    $t2, .pddone
    nop
    li      $t2, 10
    mul     $v0, $v0, $t2
    addu    $v0, $v0, $t1
    addiu   $a0, $a0, 1
    j       .pdloop
    nop
.pddone:
    jr      $ra
    nop

/* ============================================================
 * Exit / Error handlers
 * ============================================================ */
exit_ok:
    move    $a0, $s1
    li      $v0, __NR_close
    syscall
#ifdef DAEMON_MODE
    j       _cmd_loop
    nop
#else
    li      $a0, 0
    li      $v0, __NR_exit
    syscall
#endif

usage:
    EPRINT usage_msg
    j       exit_err
    nop

usage_walk:
    EPRINT usage_walk_msg
    j       exit_err
    nop

err_open:
    EPRINT open_err_msg
    j       exit_err
    nop

err_seek:
    EPRINT seek_err_msg

exit_err:
#ifdef DAEMON_MODE
    /* Close devmem fd if open ($s1 = -1 if not) */
    move    $a0, $s1
    li      $v0, __NR_close
    syscall
    j       _cmd_loop
    nop
#else
    li      $a0, 1
    li      $v0, __NR_exit
    syscall
#endif

/* ============================================================
 * Data
 * ============================================================ */
    .data
/* Command name strings for dispatch */
_cmd_read:      .asciz "read"
_cmd_walk:      .asciz "walk"
_cmd_slab:      .asciz "slab"
_cmd_pages:     .asciz "pages"
_cmd_write:     .asciz "write"
_cmd_flush:     .asciz "flush"
_cmd_find:      .asciz "find"
_cmd_dump:      .asciz "dump"
_cmd_string:    .asciz "string"

devpath:        .asciz "/var/tmp/devmem"
hex_prefix:     .ascii "0x"
hex_prefix_end:
sep:            .ascii ": 0x"
sep_end:
newline:        .ascii "\n"
newline_end:
walk_prefix:    .ascii "#"
walk_prefix_end:
walk_sep:       .ascii ": "
walk_sep_end:
walk_total:     .ascii "total: "
walk_total_end:
usage_msg:      .ascii "Usage:\n  kpoke read <addr> [count]\n  kpoke walk <list_head> <offset> [max]\n  kpoke slab <cache_addr>\n  kpoke pages <cache_addr>\n  kpoke write <addr> <val> [count]\n  kpoke flush <addr> <nbytes>\n"
usage_msg_end:
usage_walk_msg: .ascii "Usage: kpoke walk <list_head_addr> <entry_offset> [max]\n"
usage_walk_msg_end:
open_err_msg:   .ascii "Error: cannot open /dev/mem\n"
open_err_msg_end:
seek_err_msg:   .ascii "Error: lseek failed\n"
seek_err_msg_end:
pages_hdr_full:   .ascii "=== slabs_full ===\n"
pages_hdr_full_end:
pages_hdr_part:   .ascii "=== slabs_partial ===\n"
pages_hdr_part_end:
pages_slab_pre:   .ascii "slab "
pages_slab_pre_end:
pages_at:         .ascii " @ "
pages_at_end:
pages_inuse:      .ascii " inuse="
pages_inuse_end:
pages_obj_pre:    .ascii "  ["
pages_obj_pre_end:
pages_obj_mid:    .ascii "] 0x"
pages_obj_mid_end:
pages_space:      .ascii " "
pages_space_end:
pages_slabs_suffix: .ascii " slabs\n"
pages_slabs_suffix_end:
slab_warn_msg:  .ascii "Error: name pointer invalid — wrong NR_CPUS? (expected NR_CPUS=1)\n"
slab_warn_msg_end:
slab_f_name:    .ascii "  name:        "
slab_f_name_end:
slab_f_bufsz:   .ascii "  buffer_size: "
slab_f_bufsz_end:
slab_f_num:     .ascii "  num:         "
slab_f_num_end:
slab_f_order:   .ascii "  gfporder:    "
slab_f_order_end:
slab_f_batch:   .ascii "  batchcount:  "
slab_f_batch_end:
slab_f_limit:   .ascii "  limit:       "
slab_f_limit_end:
slab_f_flags:   .ascii "  flags:       "
slab_f_flags_end:
slab_f_colour:  .ascii "  colour:      "
slab_f_colour_end:
slab_f_coloff:  .ascii "  colour_off:  "
slab_f_coloff_end:
slab_f_slabsz:  .ascii "  slab_size:   "
slab_f_slabsz_end:
slab_f_node:    .ascii "  nodelists:   "
slab_f_node_end:
slab_paren_open:  .ascii " ("
slab_paren_open_end:
slab_paren_close: .ascii ")\n"
slab_paren_close_end:
flush_ok_msg:   .ascii "Cache flushed OK\n"
flush_ok_msg_end:
flush_err_msg:  .ascii "Error: cacheflush failed\n"
flush_err_msg_end:
todo_msg:       .ascii "Not implemented yet\n"
todo_msg_end:
