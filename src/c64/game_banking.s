        .section .bss.sst64_banking,"aw",@nobits
        .globl sst64_requested_bank, sst64_target, sst64_active_bank
sst64_requested_bank: .space 1
sst64_target: .space 2
sst64_active_bank: .space 1
sst64_depth: .space 1
sst64_callers: .space 64

        .section .init.220,"ax",@progbits
        lda #255
        sta sst64_active_bank

        .section .text.sst64_banking,"ax",@progbits
        .globl sst64_enter
sst64_enter:
        lda __rc1
        cmp #$c0
        bcc sst64_banking_fail
        ldx sst64_depth
        cpx #64
        bcs sst64_banking_fail
        lda sst64_active_bank
        sta sst64_callers,x
        inc sst64_depth
        lda sst64_requested_bank
        jsr sst64_select_bank
        pla
        tay
        pla
        tax
        pla
        plp
        sta sst64_return_a
        php
        pla
        sta sst64_return_flags
        lda sst64_return_a
        pha
        lda #mos16hi(sst64_return-1)
        pha
        lda #mos16lo(sst64_return-1)
        pha
        lda sst64_return_flags
        pha
        lda sst64_return_a
        plp
        jmp (sst64_target)

sst64_return:
        php
        pha
        txa
        pha
        tya
        pha
        dec sst64_depth
        ldx sst64_depth
        lda sst64_callers,x
        cmp #255
        beq sst64_return_restored
        jsr sst64_select_bank
sst64_return_restored:
        pla
        tay
        pla
        tax
        pla
        sta sst64_return_a
        pla
        sta sst64_return_flags
        pla
        lda sst64_return_flags
        pha
        lda sst64_return_a
        plp
        rts

sst64_select_bank:
        cmp sst64_active_bank
        beq sst64_selected
        sta sst64_active_bank
        php
        sei
        lda $df00
        lda #0
        sta $df09
        sta $df0a
        sta $df02
        sta $df04
        sta $df07
        lda #$80
        sta $df03
        lda #$40
        sta $df08
        lda sst64_active_bank
        lsr
        lsr
        sta $df06
        lda sst64_active_bank
        and #3
        asl
        asl
        asl
        asl
        asl
        asl
        sta $df05
        lda #$91
        sta $df01
        lda $df00
        and #$40
        beq sst64_banking_fail
        plp
sst64_selected:
        rts

sst64_banking_fail:
        jmp sst64_fail

        .section .bss.sst64_banking,"aw",@nobits
sst64_return_a: .space 1
sst64_return_flags: .space 1