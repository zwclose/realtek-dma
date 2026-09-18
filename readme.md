## PoC for CVE-2022-25476: arbitrary physical memory access via Realtek SD card reader driver / DMA controller

This repository contains a PoC exploit for CVE-2022-25476, a vulnerability in the Realtek SD card reader driver that allows non-privileged users to access physical memory via the DMA controller.

For the full technical breakdown, see the [blog post](https://zwclose.github.io/2026/07/08/rtsper2.html).

### Overview

The PoC implements both physical memory reads and writes. Currently, transfers are limited to 512 bytes at a time; support for larger transfers will be added in the future. The solution consists of two projects:

* **realtek-dma** -- the PoC exploit
* **target** -- a helper project for testing

When started, the PoC presents the following commands:

* **r** -- read from physical memory
* **w** -- write to physical memory

### Reading Physical Memory

The read command prompts the user for a physical memory address and an SD card sector, then programs the DMA controller to transfer data from the specified physical address to the specified sector. Both the source physical address and the target sector can be omitted, in which case the PoC defaults to physical address 0x1000 and sector 1 as the safest bet.

### Writing Physical Memory

The write command prompts for an SD card sector and a target physical memory address, then transfers the sector contents to the specified memory address.

Omitting the sector defaults to sector 0. Omitting the target address is a bit more tricky. Because writing to physical memory can be dangerous and identifying a suitable target address can be difficult, the PoC allows it to be omitted. In this case, the driver's internal command buffer is used as the DMA destination. After the transfer completes, the PoC copies the contents of the command buffer into a user-mode buffer and prints them to the screen. This option is convenient for quickly testing the PoC.

## Testing

The target project is a puppet process for PoC testing. Its purpose is to allocate memory regions that can be read from or written to using the PoC.

The project implements two commands: allocate memory and dump the contents of a virtual address.

To test the PoC, start the target process, allocate a memory region, determine its physical addresses using RAMMap, and feed them to the PoC. Once the DMA operation completes, verify the result with the dump command.

For details, see the Demo section of the [blog post](https://zwclose.github.io/2026/07/08/rtsper2.html).

## Notes

* Do not forget to insert an SD card before running the PoC.
* Make sure the DmaRemappingCompatible value in the driver's Parameters registry key is set to 0.
* The PoC requires a vulnerable version of the Realtek SD card reader driver (10.0.26100.21374 or earlier).
* The PoC was tested on the RTS5260 but is expected to work on other Realtek card reader models as well.

## Disclaimer

This PoC is provided for research and educational purposes only. Writing to arbitrary physical memory can corrupt data, crash the operating system, and cause all sorts of other terrible things to happen.

Use with care!