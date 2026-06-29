\
\ Fcode payload for Xbox 360 "Xenos" graphics card
\
\ This is the Forth source for an Fcode payload to initialise
\ the Xbox 360 "Xenos" graphics card.
\
\ (C) Copyright 2026 John Davis
\

fcode-version3

\
\ Dictionary lookups for words that don't have an FCode
\

: (find-xt)   \ ( str len -- xt | -1 )
  $find if
    exit
  else
    2drop
    -1
  then
;

h# 500 constant xenos-video-width
h# 2D0 constant xenos-video-height
h# 20 constant xenos-depth-bits
h# 1400 constant xenos-line-bytes


" openbios-video-width" (find-xt) cell+ value openbios-video-width-xt
" openbios-video-height" (find-xt) cell+ value openbios-video-height-xt
" depth-bits" (find-xt) cell+ value depth-bits-xt
" line-bytes" (find-xt) cell+ value line-bytes-xt

" fb8-fillrect" (find-xt) value fb8-fillrect-xt
: fb8-fillrect fb8-fillrect-xt execute ;

" pci-bar>pci-addr" (find-xt) value pci-bar>pci-addr-xt
: pci-bar>pci-addr pci-bar>pci-addr-xt execute ;

h# 10 constant cfg-bar0    \ MMIO BAR
-1 value xenos-mmio-base

: map-mmio ( -- )
  cfg-bar0 pci-bar>pci-addr if   \ ( pci-addr.lo pci-addr.mid pci-addr.hi size )
    " pci-map-in" $call-parent
    to xenos-mmio-base
  then
;

: read-xenos-mmio ( regoffset -- value )
  xenos-mmio-base + l@
;

: write-xenos-mmio ( value regoffset -- )
  xenos-mmio-base + l!
;

external

: color!  ( r g b c# -- )
  \ TODO: Not implementing this function results in a stack overflow
  \ due to broken code, for now just pop the 4 from the stack.
  2drop
  2drop
;

: fill-rectangle  ( color_ind x y width height -- )
  fb8-fillrect
;

: dimensions  ( -- width height )
  xenos-video-width
  xenos-video-height
;

headerless

\
\ Installation
\

" display" device-type

: xenos-driver-install ( -- )
  xenos-mmio-base -1 = if
    map-mmio
  then

  \ Get the framebuffer from D1GRPH_PRIMARY_SURFACE_ADDRESS.
  h# 6110 read-xenos-mmio to frame-buffer-adr
  default-font set-font

  frame-buffer-adr encode-int " address" property
  frame-buffer-adr encode-int h# 384000 encode-int encode+ " AAPL,vram-memory" property

  xenos-video-width xenos-video-height over char-width / over char-height /
  fb8-install
;

: xenos-driver-init
  \ Set the dictionary properties for the video system.
  xenos-video-height openbios-video-height-xt !
  xenos-video-width openbios-video-width-xt !
  xenos-depth-bits depth-bits-xt !
  xenos-line-bytes line-bytes-xt !

  \ Device-level properties.
  xenos-video-height encode-int " height" property
  xenos-video-width encode-int " width" property
  xenos-depth-bits encode-int " depth" property
  xenos-line-bytes encode-int " linebytes" property

  ['] xenos-driver-install is-install
;

xenos-driver-init

end0
