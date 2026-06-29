\   Xbox 360 specific initialization code
\
\   Copyright (C) 2026 John Davis
\
\   This program is free software; you can redistribute it and/or
\   modify it under the terms of the GNU General Public License
\   as published by the Free Software Foundation
\

include config.fs

\ ---------
\ DMA words
\ ---------

: ppc-dma-free  ( virt size -- )
  2drop
;

: ppc-dma-map-out  ( virt devaddr size -- )
  (dma-sync)
;

['] ppc-dma-free to (dma-free)
['] ppc-dma-map-out to (dma-map-out)

\ -------------------------------------------------------------
\ device-tree
\ -------------------------------------------------------------

" /" find-device
\ Apple calls the root node device-tree
" device-tree" device-name
" bootrom" device-type
" 0000000000000" encode-string " system-id" property
1 encode-int " #address-cells" property
1 encode-int " #size-cells" property
1 encode-int " #interrupt-cells" property
" MSFT,Xenon" model
" Xenon" encode-string " MSFT,Xenon" encode-string encode+ " compatible" property \ TODO: System Profiler takes the first string in compatible
\ Bus frequency on the root node
h# 2faf080 encode-int " clock-frequency" property

    : dma-sync
      (dma-sync)
    ;

    : dma-alloc
      (dma-alloc)
    ;

    : dma-free
      (dma-free)
    ;

    : dma-map-in
      (dma-map-in)
    ;

    : dma-map-out
      (dma-map-out)
    ;

new-device
    " cpus" device-name
    1 encode-int " #address-cells" property
    0 encode-int " #size-cells" property
    external

    : encode-unit ( unit -- str len )
        pocket tohexstr
    ;

    : decode-unit ( str len -- unit )
        parse-hex
    ;

finish-device

new-device
    " memory" device-name
    " memory" device-type
    external
    : open true ;
    : close ;
finish-device

\ ROM device required for classic Mac OS
" /" find-device
new-device
  " rom" device-name
finish-device

\ -------------------------------------------------------------
\ Base hardware devices on both Wii and Wii U. Platform-specific items are addeded later.
\ -------------------------------------------------------------

\ Interrupt controller
new-device
  " interrupt-controller" device-name
  " interrupt-controller" device-type
  " MSFT,xenon-ic" model
  " xenon-ic" encode-string " compatible" property
  " " encode-string " built-in" property
  " " encode-string " interrupt-controller" property
  1 encode-int " #interrupt-cells" property
\ Interrupt controller is at 0x20000050000 / 0x6000 but XNU cannot handle 64-bit physical addresses here
\ MMIO range omitted and is directly mapped by XNU instead
  h# ea000000 encode-int h# 100 encode-int encode+
    h# e1000000 encode-int encode+ h# 2000000 encode-int encode+ " reg" property

  external
  : open true ;
  : close ;
finish-device

\ -------------------------------------------------------------
\ /packages
\ -------------------------------------------------------------

" /packages" find-device

    " packages" device-name
    external
    \ allow packages to be opened with open-dev
    : open true ;
    : close ;

\ /packages/terminal-emulator
new-device
    " terminal-emulator" device-name
    external
    : open true ;
    : close ;
    \ : write ( addr len -- actual )
    \	dup -rot type
    \ ;
finish-device

\ -------------------------------------------------------------
\ The END
\ -------------------------------------------------------------
device-end
