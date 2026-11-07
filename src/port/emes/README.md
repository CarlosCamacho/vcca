# emesDOS09 controller core

The portable PIC32 firmware core of [emesDOS09](https://github.com/CarlosCamacho/EmEsDOS09),
copied unmodified (MIT, see `LICENSE-emesdos09`) from `HARDWARE/emesDOS09/firmware/pic32/` at
commit `7c3115d` (branch `dev/host-mailbox`). `host_mailbox.c` wraps it behind the cartridge's
`$FF40-$FF4F` mailbox; `port/pak.cpp` (`EmesCart`) calls it. To update, copy those files again and
change the commit here. Protocol 2.2 only; the ROM falls back from 2.3 on its own.
