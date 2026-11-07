# Wii U Menu code patches for themes (see codepatch.cpp).
#
# Each cave is entered from a 4-word detour written over a block of Menu code:
#     lis r12,cave@h / ori r12,r12,cave@l / mtlr r12 / blrl
# so on entry LR = block start + 16 and r12 holds the cave address. A cave does the patched work, runs the other
# instructions of the block (recreated here), and continues at block start + 16 (or wherever the block's own branch
# would have gone). The blocks were checked in every supported build: identical words, no branch target inside.
#
# gCave (filled by codepatch.cpp for the running build):
#   0x00 show   0x04 is_shown   0x08 set_visible   0x0C set_black   0x10 set_white   0x14 find_picture
#   0x18 centre_global   0x1C x_off (float)   0x20 z_off (float)
# Registers are written as numbers (r3 = 3, f11 = 11).

    .section .text
    .align 4

# Load the gCave word at offset \off into r12 and call it.
.macro CALL_CAVE off
    lis     12, gCave@ha
    addi    12, 12, gCave@l
    lwz     12, \off(12)
    mtctr   12
    bctrl
.endm

# ---------------------------------------------------------------------------------------------------------------
# Plaza icon ring, block 1: replaces  fneg f11,f8 / stfs f26,4(r4) / fmuls f0,f21,f25 / addi r23,r23,1
# x = x_off - f8 (stock: -f8), z = f21 * f25 + z_off (stock: f21 * f25). r12 is free here.
    .global cave_plaza1
cave_plaza1:
    lis     12, gCave@ha
    addi    12, 12, gCave@l
    lfs     11, 0x1C(12)
    fsubs   11, 11, 8
    stfs    26, 4(4)
    lfs     0, 0x20(12)
    fmadds  0, 21, 25, 0
    addi    23, 23, 1
    blr

# Plaza icon ring, block 2: same, with stfs f26,4(r31)
    .global cave_plaza2
cave_plaza2:
    lis     12, gCave@ha
    addi    12, 12, gCave@l
    lfs     11, 0x1C(12)
    fsubs   11, 11, 8
    stfs    26, 4(31)
    lfs     0, 0x20(12)
    fmadds  0, 21, 25, 0
    addi    23, 23, 1
    blr

# ---------------------------------------------------------------------------------------------------------------
# Plaza centre slot: replaces  stfs f28,0(r31) / stfs f28,8(r31) / lwzu r0,G@l(r24) / addi r31,r27,0x1c8
# Stores x_off and z_off as the centre's x and z (stock: 0). The lwzu (r24 = G after "lis r24,G@ha" before the
# block, r0 = *G) is recreated with this build's G from gCave.
    .global cave_centre
cave_centre:
    lis     12, gCave@ha
    addi    12, 12, gCave@l
    lwz     0, 0x1C(12)
    stw     0, 0(31)
    lwz     0, 0x20(12)
    stw     0, 8(31)
    lwz     24, 0x18(12)
    lwz     0, 0(24)
    addi    31, 27, 0x1C8
    blr

# ---------------------------------------------------------------------------------------------------------------
# Launcher page 1: replaces  bl show / lmw r25,0x14(r1) / lwz r0,0x34(r1) / mtlr r0   (the end of the arrow
# update; the next words return to its caller). After the stock "show right arrow" call: in the launcher only
# (arrow object r29 +0x50 == 0) the right arrow's panes P_BtnSlideR and PF_RBtnMask get the visibility of the left
# arrow (r26), so page 1 (no left arrow) hides them while the arrow stays usable.
    .global cave_page1
cave_page1:
    mflr    12
    stwu    1, -0x20(1)
    stw     12, 0x08(1)
    stw     31, 0x1C(1)
    stw     30, 0x18(1)
    CALL_CAVE 0x00                  # show(r3, r4, ...): the stock call
    lbz     0, 0x50(29)
    cmpwi   0, 0
    bne     9f
    mr      3, 26
    CALL_CAVE 0x04                  # is_shown(left arrow)
    mr      31, 3
    lis     30, str_P_BtnSlideR@ha
    addi    30, 30, str_P_BtnSlideR@l
    bl      page1_set
    lis     30, str_PF_RBtnMask@ha
    addi    30, 30, str_PF_RBtnMask@l
    bl      page1_set
9:
    lwz     12, 0x08(1)
    lwz     31, 0x1C(1)
    lwz     30, 0x18(1)
    addi    1, 1, 0x20
    lmw     25, 0x14(1)             # the block's own instructions
    lwz     0, 0x34(1)
    mtlr    0
    mtctr   12                      # continue after the block (CTR is free at the function's end)
    bctr

# set_visible(root->FindPaneByName(r30, true), r31) for the BtnSlideLauncher layout (r29 +0x64 +0x2c +0x1c)
page1_set:
    mflr    0
    stwu    1, -0x10(1)
    stw     0, 0x14(1)
    lwz     3, 0x64(29)
    cmpwi   3, 0
    beq     1f
    lwz     3, 0x2C(3)
    cmpwi   3, 0
    beq     1f
    lwz     3, 0x1C(3)
    cmpwi   3, 0
    beq     1f
    lwz     12, 8(3)
    lwz     12, 0x5C(12)
    mtctr   12
    mr      4, 30
    li      5, 1
    bctrl
    cmpwi   3, 0
    beq     1f
    mr      4, 31
    CALL_CAVE 0x08                  # set_visible(pane, visible)
1:
    lwz     0, 0x14(1)
    mtlr    0
    addi    1, 1, 0x10
    blr

# ---------------------------------------------------------------------------------------------------------------
# Folder footer: replaces  bl set_black / lwz r3,0x194(r30) / cmpwi r3,0 / beq +0x80   in the folder color setter
# (r30 = FolderBase layout, r31 = white color, r28 = the setter's name-object vtable). After the stock call, the
# name bar (BtnFolderName in part L_FolderName), the settings corner (P_Shadow_01 in part L_BtnSetting) and the back
# corner (FolderOut in the back button's layout, FolderBase +0x1a8 +0x5c +0x1c) take the white color too.
    .global cave_folder
cave_folder:
    mflr    12
    stwu    1, -0x20(1)
    stw     12, 0x08(1)
    stw     29, 0x1C(1)
    CALL_CAVE 0x0C                  # set_black(r3, r4): the stock call
    lis     4, str_L_FolderName@ha
    addi    4, 4, str_L_FolderName@l
    lis     5, str_BtnFolderName@ha
    addi    5, 5, str_BtnFolderName@l
    bl      folder_tint_part
    lis     4, str_L_BtnSetting@ha
    addi    4, 4, str_L_BtnSetting@l
    lis     5, str_P_Shadow_01@ha
    addi    5, 5, str_P_Shadow_01@l
    bl      folder_tint_part
    lwz     3, 0x1A8(30)
    cmpwi   3, 0
    beq     8f
    lwz     3, 0x5C(3)
    cmpwi   3, 0
    beq     8f
    lwz     29, 0x1C(3)
    cmpwi   29, 0
    beq     8f
    lis     4, str_FolderOut@ha
    addi    4, 4, str_FolderOut@l
    bl      folder_tint
8:
    lwz     12, 0x08(1)
    lwz     29, 0x1C(1)
    addi    1, 1, 0x20
    lwz     3, 0x194(30)            # the block's own instructions
    cmpwi   3, 0
    bne     9f
    addi    12, 12, 0x7C            # beq +0x80 from the block's 4th word = block start + 16 + 0x7C
9:
    mtctr   12                      # CTR is free right after a call
    bctr

# set_white(root r29 ->FindPaneByName(r4, true), r31)
folder_tint:
    mflr    0
    stwu    1, -0x10(1)
    stw     0, 0x14(1)
    mr      3, 29
    lwz     12, 8(3)
    lwz     12, 0x5C(12)
    mtctr   12
    li      5, 1
    bctrl
    cmpwi   3, 0
    beq     1f
    mr      4, 31
    CALL_CAVE 0x10                  # set_white(pane, color)
1:
    lwz     0, 0x14(1)
    mtlr    0
    addi    1, 1, 0x10
    blr

# set_white(find_picture(FolderBase r30, {r4, r28}, {r5, r28}), r31): the setter's own (part, pane) lookup
folder_tint_part:
    mflr    0
    stwu    1, -0x20(1)
    stw     0, 0x24(1)
    stw     4, 0x08(1)
    stw     28, 0x0C(1)
    stw     5, 0x10(1)
    stw     28, 0x14(1)
    mr      3, 30
    addi    4, 1, 0x08
    addi    5, 1, 0x10
    CALL_CAVE 0x14                  # find_picture
    cmpwi   3, 0
    beq     1f
    mr      4, 31
    CALL_CAVE 0x10                  # set_white(picture, color)
1:
    lwz     0, 0x24(1)
    mtlr    0
    addi    1, 1, 0x20
    blr

    .section .rodata
str_P_BtnSlideR:   .asciz "P_BtnSlideR"
str_PF_RBtnMask:   .asciz "PF_RBtnMask"
str_L_FolderName:  .asciz "L_FolderName"
str_BtnFolderName: .asciz "BtnFolderName"
str_L_BtnSetting:  .asciz "L_BtnSetting"
str_P_Shadow_01:   .asciz "P_Shadow_01"
str_FolderOut:     .asciz "FolderOut"
