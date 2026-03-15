/*
 * kpoked: TCP daemon for kpoke — kernel memory inspection over the network
 *
 * Usage: kpoked <port>
 *
 * Listens on TCP port, accepts connections, reads commands line-by-line.
 * Each connection is fork()'d — child handles one command and exits.
 * Output goes directly to the socket via dup2().
 */

#include <sys/syscall.h>

#define SEEK_SET 0
#define O_RDONLY 0
#define O_RDWR   2

#define AF_INET      2
#define SOCK_STREAM  2
#define SOL_SOCKET   0xFFFF  /* MIPS-specific, not 1 like x86 */
#define SO_REUSEADDR 4
#define INADDR_ANY   0

/* Line buffer size */
#define LINEBUF_SIZE 256
/* Max argv entries */
#define MAX_ARGC 16

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

__start:
    lw      $t0, 0($sp)         /* argc */
    slti    $v0, $t0, 2
    bnez    $v0, .d_usage
    nop

    /* Parse port number from argv[1] */
    lw      $a0, 8($sp)         /* argv[1] */
    jal     parse_dec
    nop
    move    $s2, $v0            /* s2 = port number */

    /* socket(AF_INET, SOCK_STREAM, 0) */
    li      $a0, AF_INET
    li      $a1, SOCK_STREAM
    li      $a2, 0
    li      $v0, __NR_socket
    syscall
    bltz    $v0, .d_err_socket
    nop
    move    $s0, $v0            /* s0 = server fd */

    /* setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &1, 4) */
    addiu   $sp, $sp, -24       /* frame: 16 arg save + 4 optval + 4 pad */
    li      $t0, 1
    sw      $t0, 0($sp)         /* optval = 1 */
    move    $a0, $s0
    li      $a1, SOL_SOCKET
    li      $a2, SO_REUSEADDR
    move    $a3, $sp             /* &optval */
    li      $t0, 4
    sw      $t0, 16($sp)        /* optlen = 4 (5th arg at sp+16) */
    li      $v0, __NR_setsockopt
    syscall
    addiu   $sp, $sp, 24

    /* Build sockaddr_in on stack */
    addiu   $sp, $sp, -16
    sh      $zero, 0($sp)       /* sin_family placeholder */
    li      $t0, AF_INET
    sh      $t0, 0($sp)         /* sin_family = AF_INET */

    /* htons(port): swap bytes for network order on LE */
    andi    $t0, $s2, 0xFF
    sll     $t0, $t0, 8
    srl     $t1, $s2, 8
    andi    $t1, $t1, 0xFF
    or      $t0, $t0, $t1
    sh      $t0, 2($sp)         /* sin_port = htons(port) */
    sw      $zero, 4($sp)       /* sin_addr = INADDR_ANY */
    sw      $zero, 8($sp)       /* padding */
    sw      $zero, 12($sp)      /* padding */

    /* bind(fd, &sockaddr, 16) */
    move    $a0, $s0
    move    $a1, $sp
    li      $a2, 16
    li      $v0, __NR_bind
    syscall
    bnez    $a3, .d_err_bind
    nop

    addiu   $sp, $sp, 16        /* free sockaddr */

    /* listen(fd, 5) */
    move    $a0, $s0
    li      $a1, 5
    li      $v0, __NR_listen
    syscall

    /* Print listening message */
    EPRINT d_listening_msg

    /* Print port number to stderr */
    move    $a0, $s2
    /* print_dec writes to stdout (fd 1), we want stderr — just use it,
       the port number on stdout is fine */
    jal     print_dec
    nop
    EPRINT newline

/* ============================================================
 * Accept loop
 * ============================================================ */
.d_accept_loop:
    /* accept(server_fd, NULL, NULL) */
    move    $a0, $s0
    li      $a1, 0
    li      $a2, 0
    li      $v0, __NR_accept
    syscall
    bltz    $v0, .d_accept_loop /* retry on error */
    nop
    move    $s1, $v0            /* s1 = client fd (temporarily) */

    /* fork() */
    li      $v0, __NR_fork
    syscall

    bnez    $v0, .d_parent
    nop

    /* ===== CHILD PROCESS ===== */

    /* Close server fd */
    move    $a0, $s0
    li      $v0, __NR_close
    syscall

    /* dup2(client_fd, 0) — stdin */
    move    $a0, $s1
    li      $a1, 0
    li      $v0, __NR_dup2
    syscall

    /* dup2(client_fd, 1) — stdout */
    move    $a0, $s1
    li      $a1, 1
    li      $v0, __NR_dup2
    syscall

    /* dup2(client_fd, 2) — stderr */
    move    $a0, $s1
    li      $a1, 2
    li      $v0, __NR_dup2
    syscall

    /* Close original client fd (now duped to 0/1/2) */
    move    $a0, $s1
    li      $v0, __NR_close
    syscall

    /* Read command line from stdin */
    la      $s4, d_linebuf
    li      $a0, 0              /* fd = stdin */
    move    $a1, $s4            /* buf */
    li      $a2, LINEBUF_SIZE - 1
    li      $v0, __NR_read
    syscall
    blez    $v0, .d_child_exit  /* EOF or error */
    nop
    move    $s5, $v0            /* s5 = bytes read */

    /* Null-terminate and strip trailing \r\n */
    addu    $t0, $s4, $s5
    sb      $zero, 0($t0)       /* null terminate */
.d_strip_tail:
    addiu   $t0, $t0, -1
    sltu    $t1, $t0, $s4
    bnez    $t1, .d_child_exit  /* empty line */
    nop
    lbu     $t1, 0($t0)
    li      $t2, '\n'
    beq     $t1, $t2, .d_strip_char
    nop
    li      $t2, '\r'
    beq     $t1, $t2, .d_strip_char
    nop
    j       .d_tokenize
    nop
.d_strip_char:
    sb      $zero, 0($t0)
    j       .d_strip_tail
    nop

    /* Tokenize: split on spaces, build argv array */
.d_tokenize:
    la      $s6, d_argv         /* s6 = argv array base */
    move    $t0, $s4            /* t0 = cursor into linebuf */
    li      $s3, 0              /* s3 = argc */

    /* argv[0] = "kpoked" (dummy program name) */
    la      $t1, d_progname
    sw      $t1, 0($s6)
    addiu   $s3, $s3, 1

    /* Skip leading spaces */
.d_tok_skip:
    lbu     $t1, 0($t0)
    beqz    $t1, .d_tok_done
    nop
    li      $t2, ' '
    beq     $t1, $t2, .d_tok_advance_skip
    nop
    li      $t2, '\t'
    beq     $t1, $t2, .d_tok_advance_skip
    nop
    j       .d_tok_save
    nop
.d_tok_advance_skip:
    addiu   $t0, $t0, 1
    j       .d_tok_skip
    nop

    /* Save token start */
.d_tok_save:
    slti    $t1, $s3, MAX_ARGC
    beqz    $t1, .d_tok_done    /* too many args */
    nop
    sll     $t1, $s3, 2         /* offset = argc * 4 */
    addu    $t1, $s6, $t1
    sw      $t0, 0($t1)         /* argv[argc] = token start */
    addiu   $s3, $s3, 1

    /* Scan to next space or end */
.d_tok_scan:
    lbu     $t1, 0($t0)
    beqz    $t1, .d_tok_done
    nop
    li      $t2, ' '
    beq     $t1, $t2, .d_tok_split
    nop
    li      $t2, '\t'
    beq     $t1, $t2, .d_tok_split
    nop
    addiu   $t0, $t0, 1
    j       .d_tok_scan
    nop

.d_tok_split:
    sb      $zero, 0($t0)       /* null-terminate token */
    addiu   $t0, $t0, 1
    j       .d_tok_skip
    nop

.d_tok_done:
    /* s3 = argc, s6 = argv — dispatch */
    slti    $v0, $s3, 2
    bnez    $v0, .d_child_exit  /* no command */
    nop

    DISPATCH read,   cmd_read_named
    DISPATCH walk,   cmd_walk
    DISPATCH slab,   cmd_slab
    DISPATCH pages,  cmd_pages
    DISPATCH write,  cmd_write
    DISPATCH flush,  cmd_flush
    DISPATCH find,   cmd_find
    DISPATCH dump,   cmd_dump
    DISPATCH string, cmd_string

    /* Unknown command — try as hex address (legacy) */
    j       cmd_read_legacy
    nop

.d_child_exit:
    li      $a0, 0
    li      $v0, __NR_exit
    syscall

    /* ===== PARENT PROCESS ===== */
.d_parent:
    /* Close client fd in parent */
    move    $a0, $s1
    li      $v0, __NR_close
    syscall

    /* waitpid(-1, NULL, WNOHANG) to reap zombies */
    li      $a0, -1
    li      $a1, 0
    li      $a2, 1              /* WNOHANG */
    li      $v0, __NR_waitpid
    syscall

    j       .d_accept_loop
    nop

/* ============================================================
 * Daemon error handlers
 * ============================================================ */
.d_usage:
    EPRINT d_usage_msg
    li      $a0, 1
    li      $v0, __NR_exit
    syscall

.d_err_socket:
    EPRINT d_err_socket_msg
    li      $a0, 1
    li      $v0, __NR_exit
    syscall

.d_err_bind:
    addiu   $sp, $sp, 16        /* free sockaddr */
    EPRINT d_err_bind_msg
    li      $a0, 1
    li      $v0, __NR_exit
    syscall

/* ============================================================
 * Daemon-specific data
 * ============================================================ */
    .data
d_progname:       .asciz "kpoked"
d_usage_msg:      .ascii "Usage: kpoked <port>\n"
d_usage_msg_end:
d_listening_msg:  .ascii "Listening on port "
d_listening_msg_end:
d_err_socket_msg: .ascii "Error: socket() failed\n"
d_err_socket_msg_end:
d_err_bind_msg:   .ascii "Error: bind() failed\n"
d_err_bind_msg_end:

    .bss
d_linebuf:  .space LINEBUF_SIZE
d_argv:     .space (MAX_ARGC * 4)

/* Include shared commands, helpers, and data */
    .text
#include "common.inc"
