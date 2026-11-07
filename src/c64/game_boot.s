        .include "build/game-boot.inc"
        .section .text,"ax",@progbits
        .globl main
main:
        cld
        lda #0
        sta $d020
        sta $d021
        lda #5
        sta $0286
        lda #147
        jsr $ffd2
        ldx #0
copy_loader:
        lda loader_start,x
        sta $c000,x
        lda loader_start+256,x
        sta $c100,x
        inx
        bne copy_loader
        jmp $c000

; Run above CORE's load range so KERNAL LOAD cannot overwrite the loader.
loader_start:
        lda #0
        sta $df02
        sta $df08
        lda #$c2
        sta $df03
        lda #$e0
        sta $df04
        lda #$ff
        sta $df05
        lda #7
        sta $df06
        lda #8
        sta $df07
        lda #$91
        jsr $c000+(loader_dma-loader_start)
        beq loader_cold
        ldx #0
loader_compare:
        lda $c200,x
        cmp $c000+(loader_tag-loader_start),x
        bne loader_cold
        inx
        cpx #8
        bne loader_compare
        lda #$91
        sta $c000+(loader_command-loader_start)
        bne loader_core
loader_cold:
        lda #4
        ldx #mos16lo($c000+(loader_name-loader_start))
        ldy #mos16hi($c000+(loader_name-loader_start))
        jsr $ffbd
        lda #1
        ldx #8
        ldy #1
        jsr $ffba
        lda #0
        jsr $ffd5
        bcc loader_loaded
        jmp $c000+(loader_failed-loader_start)
loader_loaded:
        cpx #mos16lo($0801+SST64_CORE_LENGTH)
        beq loader_length_high
        jmp $c000+(loader_failed-loader_start)
loader_length_high:
        cpy #mos16hi($0801+SST64_CORE_LENGTH)
        beq loader_length_ok
        jmp $c000+(loader_failed-loader_start)
loader_length_ok:
        lda #$90
        sta $c000+(loader_command-loader_start)
loader_core:
        lda #1
        sta $df02
        lda #8
        sta $df03
        lda #0
        sta $df04
        lda #$80
        sta $df05
        lda #5
        sta $df06
        lda #mos16lo(SST64_CORE_LENGTH)
        sta $df07
        lda #mos16hi(SST64_CORE_LENGTH)
        sta $df08
        lda $c000+(loader_command-loader_start)
        jsr $c000+(loader_dma-loader_start)
        beq loader_failed
        lda $c000+(loader_command-loader_start)
        cmp #$90
        bne loader_ready
        lda #mos16lo($c000+(loader_tag-loader_start))
        sta $df02
        lda #mos16hi($c000+(loader_tag-loader_start))
        sta $df03
        lda #$e0
        sta $df04
        lda #$ff
        sta $df05
        lda #7
        sta $df06
        lda #8
        sta $df07
        lda #0
        sta $df08
        lda #$90
        jsr $c000+(loader_dma-loader_start)
        beq loader_failed
loader_ready:
        jmp SST64_CORE_ENTRY
loader_failed:
        ldx #0
loader_error_text:
        lda $c000+(loader_error-loader_start),x
        beq loader_halt
        stx $c000+(loader_status-loader_start)
        jsr $ffd2
        ldx $c000+(loader_status-loader_start)
        inx
        bne loader_error_text
loader_halt:
        jmp $c000+(loader_halt-loader_start)
loader_dma:
        php
        sei
        pha
        lda $df00
        lda #0
        sta $df09
        sta $df0a
        pla
        sta $df01
        lda $df00
        and #$40
        sta $c000+(loader_status-loader_start)
        plp
        lda $c000+(loader_status-loader_start)
        rts
loader_tag:
        .incbin "build/game-tag.bin"
loader_name:
        .ascii "CORE"
loader_command:
        .byte 0
loader_status:
        .byte 0
loader_error:
        .asciz "SST64 BOOT LOAD ERROR\r"
        .space 512-(.-loader_start),0
