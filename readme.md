## PoC for CVE-2022-25476: arbitrary physical memory access via Realtek SD card reader driver / DMA Controller

This repository contains a PoC exploit for CVE-2022-25476, a vulnerability in the Realtek SD card reader driver that allows non-privileged users to access physical memory via the DMA controller.

For the full technical breakdown, see the [blog post](https://zwclose.github.io/2026/07/08/rtsper2.html).

### Overview

The PoC currently implements writing to physical memory and consists of two projects:

- realtek-dma -- the PoC itself
- target -- a helper project for testing

The PoC prompts the user to provide:

- the target physical address, and
- the SD card sector number whose contents will be DMA-written.

If the target address is provided, the PoC copies the specified SD card sector into that physical address.

If the target address is omitted, the PoC uses the driver's internal command buffer as the DMA target. In this case, after the transfer completes, the PoC copies the transferred data into a user-mode buffer and prints it.

If the sector number is omitted, the PoC defaults to sector 0.

### Testing

The target project is useful for testing. Depending on user input, it either allocates 16 MB of memory and fills it with a pattern, or dumps 512 bytes at a specified virtual address. One simple way to test the PoC is to dump a virtual address to confirm it contains the pattern, then use RAMMap to obtain the corresponding physical address and pass it to the PoC. After the DMA write, dumping the same virtual address again should show that it now contains data from the SD card sector. See the Demo section of the [blog post](https://zwclose.github.io/2026/07/08/rtsper2.html) for details.