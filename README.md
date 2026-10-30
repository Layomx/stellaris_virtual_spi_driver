# stellaris-virtual-spi-driver

Bare-metal SPI driver for the TI Stellaris **LM3S6965** (Cortex-M3), built on
the **SSI0** peripheral and run in QEMU (`-M lm3s6965evb`).
 
The automated checks exercise the firmware with the locally installed QEMU,
ARM GNU toolchain, and clangd versions. QEMU's SD model can change between
versions, so the exact response values are not portable across all QEMU
releases.

## 1. Objective
Drive a synchronous serial peripheral through its registers: clock gating, pin
mux, clock prescaling, SPI mode, frame width, FIFOs, status flags, and a
software chip select. Then use it against a real protocol (SD card in SPI mode).

## 2. Hardware: SSI as SPI
Stellaris calls it **SSI** (Synchronous Serial Interface). It is an ARM
PrimeCell PL022 derivative that supports three frame formats; this driver uses
**Freescale SPI** (`SSICR0.FRF = 0`). The LM3S6965 has **one** SSI (SSI0).

```
CPU
 |  load/store
 v
Memory-mapped registers (0x40008000)
 |
 v
SSI0: TX FIFO -> shift register -> bit clock generator <- sysclk / (CPSDVSR*(1+SCR))
 |                                 ^
 v                                 |
SPI signals:   PA2 SCK  --> device clock
               PA5 MOSI --> device data in      (SSI0Tx)
               PA4 MISO <-- device data out     (SSI0Rx)
               PD0 CS   --> device select, active low (plain GPIO)
```
Every SCK cycle shifts one bit out on MOSI **and** one bit in from MISO, which
is why `spi_transfer()` is full duplex: to read you must write (dummy 0xFF).

### Chip select
PA3 is the hardware frame signal (SSI0Fss). In Freescale SPI mode with
`SPH=0` it pulses high between frames, which would deselect a device in the
middle of a multi-byte command. So **CS is a GPIO (PD0), driven by software**
(`spi_cs_assert/deassert`). This also matches how QEMU wires the board: PD0 is
the SD/OLED chip select.

## 3. Files
`src/startup.c`, `ld/stellaris.ld`, `src/spi.c` (driver),
`include/LM3S6965_regs.h`, `src/log.c` (tiny UART logger),
`src/sd_demo.c` (SD-over-SPI protocol used as a test), and `src/main.c`.

## 4. Memory map
| Block | Address |
|---|---|
| SSI0 | `0x40008000` |
| GPIO A / D | `0x40004000` / `0x40007000` |
| System Control | `0x400FE000` |

## 5. Registers used
| Register | Address | Fields used |
|---|---|---|
| RCGC1 | `0x400FE104` | bit4 SSI0 (bit0 UART0 for the logger) |
| RCGC2 | `0x400FE108` | bit0 GPIOA, bit3 GPIOD |
| GPIOA AFSEL / DEN | `+0x420` / `+0x51C` | bits 2,4,5 (SCK, MISO, MOSI) |
| GPIOD DIR / DEN | `+0x400` / `+0x51C` | bit 0 (CS) |
| GPIOD DATA | `+0x000..0x3FC` | masked window, see below |
| SSICR0 | `0x40008000` | SCR[15:8] SPH[7] SPO[6] FRF[5:4] DSS[3:0] |
| SSICR1 | `0x40008004` | MS[2]=0 master, SSE[1], LBM[0] |
| SSIDR | `0x40008008` | write = push TX FIFO, read = pop RX FIFO |
| SSISR | `0x4000800C` | BSY[4] RFF[3] RNE[2] TNF[1] TFE[0] |
| SSICPSR | `0x40008010` | CPSDVSR, even 2..254 |

**Masked GPIO data:** address bits [9:2] of GPIODATA act as a bit mask, so
`*(GPIOD + (0x01 << 2)) = 0` clears **only PD0**, atomically, with no
read-modify-write. In GDB: `x/wx 0x40007004` is the PD0-only window.
No `GPIOPCTL` exists on this part; `AFSEL` selects the SSI function.

## 6. Initialization (`spi_init`)
1. Clock gating for SSI0, GPIOA, GPIOD (+ read-back for the required delay).
2. `AFSEL`/`DEN` on PA2/PA4/PA5. PA3 stays GPIO. CS: write the level **first**
   (high), then set `DIR`, so the pin never glitches low.
3. `SSICR1 = 0`: SSE=0 (must be disabled to configure) and MS=0 (master).
4. `SSICPSR`, then `SSICR0` (DSS, FRF, SPO, SPH, SCR).
5. `SSICR1 = SSE` (| `LBM` for loopback tests).

**Bit rate:** `SSIClk = sysclk / (CPSDVSR * (1 + SCR))`, max `sysclk/2` in
master mode. At 6 MHz: 1 MHz -> `CPSDVSR=2, SCR=2` (`CR0=0x207`, `CPSR=2`);
300 kHz -> `SCR=9` (`CR0=0x907`); 3 MHz -> `SCR=0`. The driver picks the
smallest even `CPSDVSR` whose `SCR` fits in 8 bits and never exceeds the
requested rate.

**Modes** (`SPO` = CPOL, `SPH` = CPHA): mode 0 = 0/0, 1 = 0/1, 2 = 1/0, 3 = 1/1.

Errors: invalid width (not 4..16) or unreachable rate -> `SPI_ERR_PARAM`.
At run time `spi_transfer` polls `TNF` and `RNE` with an iteration-based
timeout. A timeout returns `SPI_ERR_TIMEOUT`; the timeout is not calibrated to
real time.

## 7. API
```c
int      spi_init(const spi_config_t *cfg, uint32_t sysclk_hz);
uint32_t spi_set_bit_rate(uint32_t sysclk_hz, uint32_t bit_rate);
bool     spi_tx_ready(void);  // SR.TNF
bool     spi_rx_ready(void);  // SR.RNE
bool     spi_busy(void);      // SR.BSY
int      spi_transfer(uint16_t data, uint16_t *received);
int      spi_write(const uint16_t *data, size_t len);
int      spi_read(uint16_t *data, size_t len, uint16_t fill);
void     spi_cs_assert(void);
void     spi_cs_deassert(void);
```
The transfer API uses `uint16_t` and masks values to the configured frame
width. `spi_init` accepts frame widths from 4 through 16 bits. Functions return
`SPI_OK`, `SPI_ERR_PARAM`, or `SPI_ERR_TIMEOUT` where applicable.

## 8. What happens in `spi_transfer(0x40)`
1. Load `SSISR`, wait for `TNF` (room in TX FIFO).
2. Store `0x40` to `SSIDR`: it enters the TX FIFO.
3. Hardware moves it to the shifter and generates one SCK pulse per configured
   frame bit; on each one a
   bit leaves on MOSI (MSB first) and a bit enters from MISO. `SR.BSY`=1 meanwhile.
4. The received frame is pushed into the RX FIFO: `SR.RNE`=1.
5. Load `SSIDR`: pops the received byte.

## 9. Build
`make` produces the ELF, BIN, and linker MAP in `build/`. CMake also exports
`compile_commands.json` through the project-root symlink. The project is
freestanding and does not link libc; it links the compiler support library
`libgcc`.

## 10. Run
```
make test        # runs without and with a generated SD image
make run         # runs without an SD image
./run_test.sh build/stellaris_spi path/to/card.img
```
QEMU command (with card):
`qemu-system-arm -M lm3s6965evb -nographic -serial stdio -monitor none \
  -kernel build/stellaris_spi \
  -drive if=sd,format=raw,file=build/sd.img`

Real output (with card, abridged):
```
SSICR0 = 0x00000907  SSICR1 = 0x00000002  SSICPSR = 0x00000002  SSISR = 0x00000003
CMD0 -> R1=0x01     CMD8 -> R1=0x01   R7 echo = 0x000001AA
ACMD41 -> R1=0x00 (2 attempts)   OCR = 0x80FFFF00   SCK = 0x002DC6C0 Hz (3 MHz)
CMD17 -> R1=0x00    block 0 starts with: "STELLARIS-SPI-TEST"
```
The block-0 text comes from the host file `build/sd.img`, so it crossed the
whole path: firmware -> SSIDR -> PL022 model -> ssi-sd -> host file -> back.

## 11. Debugging with GDB
Start QEMU paused with `make debug` (terminal 1), then connect with
`gdb-multiarch build/stellaris_spi` (terminal 2).
```
(gdb) break spi_cs_assert
(gdb) continue
(gdb) x/wx 0x400073FC        # GPIOD data, all pins: PD0 = 1 (CS inactive)
(gdb) finish
(gdb) x/wx 0x400073FC        # PD0 = 0 (CS active)
(gdb) x/5wx 0x40008000       # CR0 CR1 DR SR (offset 0x0C) ... CPSR at 0x40008010
(gdb) x/wx 0x4000800C        # SR: 0x3 = TFE|TNF
(gdb) x/wx 0x40004420        # GPIOA AFSEL = 0x37 (PA0,PA1 UART + PA2,PA4,PA5 SSI)
(gdb) x/wx 0x40007004        # PD0-only masked window
(gdb) break spi_transfer
(gdb) p/x data               # frame about to be shifted out
```
Note: `x/5wx 0x40008000` also reads `DR` (offset 8): that pops the RX FIFO if it
holds data. Reading registers with side effects from GDB changes the state.

## 12. QEMU limitations (all verified against QEMU source and runs)
- **Loopback is real**: `CR1.LBM` is implemented. Section B/C tests the
  CPU -> FIFO -> DR -> FIFO -> CPU path for real, including `DSS` masking.
- **Timing and format are NOT modelled**: the PL022 model ignores `CPSR`,
  `SCR`, `SPO`, `SPH` and `FRF` ("Clock rate and format are ignored"). SCK
  frequency, polarity and phase **cannot be verified in QEMU**; the register
  values are only checked by readback. The transfer completes inside the
  `DR` write, so `BSY` is never visible and `RNE` is set immediately.
- **No scope-level signals**: MOSI/MISO/SCK/CS are not observable except
  through the behaviour of the attached devices.
- The project only configures SSI0 as a master; slave mode is not implemented
  by the firmware.
- **Clock gating is not enforced** (see the UART README).
- **Devices on the bus**: an **SD card in SPI mode** and an SSD0323 OLED share
  the bus and the CS line (PD0). While CS is low, the OLED also receives the
  SD traffic. Only the SD is exercised here; the OLED is not driven.
- **SD model depends on the QEMU version.** Without an image:
  QEMU 8.2.2 answers `R1=0x04` (illegal command) to every command; the
  current `master` source returns `0xFF` instead. The firmware accepts both
  as "no usable card".
- **ssi-sd does not resynchronise when CS rises** (8.2.2). After a response
  the model needs one more clocked byte, otherwise the first byte of the
  next command is swallowed. `sd_end()` sends it with CS low. Real cards also
  want 8 extra clocks after a response, so this is correct on hardware too.
- **CMD58 R1 is hard-coded to 0x01** in 8.2.2 (a real, initialised card
  answers 0x00). The demo logs it but does not assert on it.
- CRCs are not checked by the model; CMD0 and CMD8 still use the real CRCs.

## 13. Moving to real hardware
- Not testable here and **must be validated with a scope/logic analyser**:
  SCK frequency and the `SPO/SPH` mode on the wire; setup/hold at the chosen
  speed; CS timing; the `BSY`/`TNF`/`RNE` flags under real timing.
- Set up the PLL (typically 50 MHz) and pass the real sysclk to `spi_init`.
- Add pull-ups on MISO as the SD spec requires and check the EVB wiring.
- Remember the SD card must be initialised at <= 400 kHz (done: 300 kHz).
- Add timeouts to every polling loop; real hardware can stall.
- FIFOs: here every byte is sent and read back one by one. For speed, fill
  the TX FIFO (8 entries) before draining RX, or use the SSI interrupts/uDMA.
