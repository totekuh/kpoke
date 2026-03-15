/*
 * kpoke: Kernel memory inspection tool for embedded Linux (MIPS32 LE)
 * Standalone CLI version — parses argv and runs one command.
 */

#include <sys/syscall.h>

#define SEEK_SET 0
#define O_RDONLY 0
#define O_RDWR   2

/* Write a labelled string to stdout. Requires label_end to exist. */
.macro PRINT label
    la      $a1, \label
    li      $a2, \label\()_end - \label
    li      $a0, 1
    li      $v0, __NR_write
    syscall
.endm

/* Write a labelled string to stderr. Requires label_end to exist. */
.macro EPRINT label
    la      $a1, \label
    li      $a2, \label\()_end - \label
    li      $a0, 2
    li      $v0, __NR_write
    syscall
.endm

/* Dispatch: compare argv[1] against a command name, branch if match. */
.macro DISPATCH cmd, target
    lw      $a0, 4($s6)
    la      $a1, _cmd_\cmd
    jal     str_match
    nop
    beqz    $v0, \target
    nop
.endm

/* Print a slab field: label + hex + (decimal). $s0 must be cache base. */
.macro SLAB_FIELD label, offset
    la      $a1, \label
    li      $a2, \label\()_end - \label
    addiu   $a0, $s0, \offset
    jal     slab_print_field
    nop
.endm

    .set noreorder
    .globl __start
    .text

/* ===== Register conventions for commands =====
 * s0 = scratch / address
 * s1 = /dev/mem fd (set by devmem_open)
 * s2 = scratch
 * s3 = argc
 * s4 = scratch
 * s5 = scratch
 * s6 = argv base pointer
 * s7 = scratch
 */

__start:
    lw      $s3, 0($sp)         /* argc */
    addiu   $s6, $sp, 4         /* s6 = &argv[0] */

    slti    $v0, $s3, 2
    bnez    $v0, usage
    nop

    /* Dispatch: match full command name */
    DISPATCH read,   cmd_read_named
    DISPATCH walk,   cmd_walk
    DISPATCH slab,   cmd_slab
    DISPATCH pages,  cmd_pages
    DISPATCH write,  cmd_write
    DISPATCH flush,  cmd_flush
    DISPATCH find,   cmd_find
    DISPATCH dump,   cmd_dump
    DISPATCH string, cmd_string

    /* Default: treat argv[1] as hex address (legacy mode) */
    j       cmd_read_legacy
    nop

#include "common.inc"
