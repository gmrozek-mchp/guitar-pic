# UDPHS / AIC boot interrupt-storm on SAM9x (bug report)

**Status:** root-caused and fixed on hardware (SAM9X75 Curiosity Hybrid, marvin firmware). Fixes live in the MCC-generated tree — see the re-apply list in [`journal.md`](journal.md) (patches #11–#13) and the 2026-07-22 entry.

**Intended for:** filing with Microchip (Harmony `csp` AIC PLIB + Harmony USB device UDPHS driver).

---

## 1. Summary

On an AIC-based SAM9x/SAMA5 part running Harmony + FreeRTOS in USB **device** mode, if a USB host is attached at power-on the system **hangs during `SYS_Initialize`** before the scheduler starts (dead display, no console output past the bootloader). Attaching the host *after* boot works normally.

Root cause is an ordering defect in the AIC PLIB compounded by two UDPHS device-driver defects:

1. **`AIC_INT_Initialize()` enables every configured interrupt source and then unmasks the CPU (`__enable_irq()`) before returning — while peripheral drivers are still uninitialized.** Any source that can assert straight out of reset then storms the CPU. For UDPHS this is `ENDRESET`: `UDPHS_IEN` reset default is `0x10` (ENDRESET enabled), and if the controller is presenting a D+ pull-up (attached) with a host connected, the host drives bus resets and `ENDRESET` latches. Because the AIC source is level-sensitive and the driver has no client yet, the ISR cannot clear it → 100 % CPU in the ISR → `SYS_Initialize` never completes.
2. **The UDPHS device ISR returns without acknowledging the interrupt when it has no client** (`isOpened == false` or no event callback) — guaranteeing a storm on a level-sensitive controller.
3. **`F_DRV_USB_UDPHS_DEVICE_Initialize()` leaves the controller *attached*** (D+ pull-up enabled) because it sets `DETACH` *before* a core-reset toggle that clears it — so the device presents itself to the host before the stack has a client.

## 2. Affected components

- **AIC PLIB** (`peripheral/aic/plib_aic.c`, `AIC_INT_Initialize`) — SAM9x60 / SAM9X7x / SAMA5 family (Advanced Interrupt Controller, level-sensitive sources). NVIC-based parts (Cortex-M) mask the symptom because pending/latched behavior differs.
- **Harmony USB device UDPHS driver** v3.16.0 (`driver/usb/udphs/src/drv_usb_udphs_device.c`) — `F_DRV_USB_UDPHS_DEVICE_Tasks_ISR`, `F_DRV_USB_UDPHS_DEVICE_Initialize`.

## 3. Environment

- SAM9X75 (ARM926EJ-S), custom "Curiosity Hybrid" board.
- MPLAB Harmony 3, FreeRTOS, XC32 v5.10.
- USB device: single CDC-ACM function (perf-log sink). App opens + attaches the device from a task *after* early boot (post-splash).
- Repro is timing-sensitive: with a host attached at power-on it hangs; over repeated JTAG `nSRST` reloads it may take a variable number of tries to boot, because it is a race on whether the host is mid-bus-reset the instant `AIC_INT_Initialize` executes `__enable_irq()`. (`nSRST` does not reset the UDPHS block, so stale attached/enabled state persists across warm resets.)

## 4. Symptom & how it presents

- Cable attached at boot → totally dead: no LVDS output, no further UART/DBGU characters after the bootloader banner. The application's own `printf` is routed to the same DBGU, so "only the bootloader banner" means the app hung **before its first log line**, i.e. inside `SYS_Initialize`.
- Cable attached *after* the app is up → normal (device opens, attaches, enumerates).

## 5. Evidence (JTAG halt of the stalled core)

Halting the wedged target and reading state (SAM9X75; UDPHS @ `0xf803c000`, AIC @ `0xfffff100`):

```
bt:            FreeRTOS_IRQ_Handler <- DRV_USB_UDPHS_Tasks_ISR   (all PC samples in interrupt code)
xSchedulerRunning = 0                        ; scheduler never started
gDrvUSBUDPHSObj[0].status  = UNINITIALIZED    ; DRV_USB_UDPHS_Initialize has NOT run yet
gDrvUSBUDPHSObj[0].isOpened = false           ; no client
AIC_ISR   (0xfffff118) = 0x17 (=23)           ; UDPHS is the in-service source
AIC_IPR0  (0xfffff120) = ...bit23 set         ; UDPHS pending
UDPHS_IEN (0xf803c010) = 0x10                 ; ENDRESET enabled (reset default)
UDPHS_INTSTA(0xf803c014)= 0x3a / 0x36         ; ENDRESET (bit4) asserted (+DET_SUSPD/INT_SOF/WAKE_UP)
UDPHS_CTRL(0xf803c000) = 0x100                ; EN_UDPHS=1, DETACH=0  -> ATTACHED
```

`status == UNINITIALIZED` with the CPU pinned in the UDPHS ISR proves the storm begins *before* `DRV_USB_UDPHS_Initialize` runs — i.e. it is `AIC_INT_Initialize` enabling source 23 and unmasking IRQs that lets it fire, not anything the USB driver did.

## 6. Root-cause detail

### 6.1 `AIC_INT_Initialize` (primary)

```c
void AIC_INT_Initialize( void )
{
    __disable_irq();
    /* ... disable all, clear, pop nested ... */

    /* Configure active interrupts */
    for( ii = 0; ii < irqDataEntryCount; ++ii ) {
        AIC_REGS->AIC_SSR = AIC_SSR_INTSEL(irqData[ii].peripheralId);
        AIC_REGS->AIC_SMR = ...;
        AIC_REGS->AIC_SVR = (uint32_t) irqData[ii].handler;
        AIC_REGS->AIC_IECR = AIC_IECR_Msk;      /* <-- ENABLES the source (incl. UDPHS) */
    }

    __DSB();
    __enable_irq();                              /* <-- unmasks CPU IRQs while drivers uninitialized */
    __ISB();
}
```

Every tabled source is enabled at the AIC and then CPU IRQs are unmasked, all before `DRV_*_Initialize` runs for those peripherals. Any peripheral that can assert an interrupt out of reset will storm the CPU here. UDPHS is the concrete case (ENDRESET enabled by `UDPHS_IEN` reset default `0x10`, controller attached, host driving resets).

### 6.2 UDPHS ISR does not acknowledge with no client (secondary)

`F_DRV_USB_UDPHS_DEVICE_Tasks_ISR` early-returns on `isOpened == false` / null `pEventCallBack` after only a `SYS_DEBUG_MESSAGE`, **without clearing `UDPHS_CLRINT` or masking `UDPHS_IEN`**. The wrapper's `SYS_INT_SourceStatusClear` only touches the AIC (a no-op for a level-sensitive source whose peripheral condition persists). So an interrupt that arrives before a client exists re-fires indefinitely.

### 6.3 `F_DRV_USB_UDPHS_DEVICE_Initialize` finishes attached (secondary)

```c
usbID->UDPHS_CTRL |= UDPHS_CTRL_DETACH_Msk;     /* detach */
usbID->UDPHS_CTRL |= UDPHS_CTRL_PULLD_DIS_Msk;
usbID->UDPHS_CTRL &= ~UDPHS_CTRL_EN_UDPHS_Msk;  /* "Reset IP": disabling EN resets the core... */
usbID->UDPHS_CTRL |= UDPHS_CTRL_EN_UDPHS_Msk;   /* ...which CLEARS the DETACH set two lines up */
```

The core-reset toggle clears `DETACH`, so init returns with the D+ pull-up **connected** (`CTRL=0x100`). The device therefore presents itself to a host before the application has opened/attached it.

## 7. Fixes applied (and suggested upstream)

### Fix 1 — `AIC_INT_Initialize`: leave UDPHS masked until its driver is ready (the boot fix)

After the configure loop, **before `__enable_irq()`**, keep the source disabled (handler/priority stay configured):

```c
    /* Leave UDPHS (source 23) masked until the USB device stack is ready.
     * Its handler/priority are configured above, but the source stays
     * disabled: with a host attached at boot the controller asserts ENDRESET
     * before the driver has a client, and __enable_irq() below would deliver
     * it immediately, storming the CPU. The app enables it via
     * SYS_INT_SourceEnable(UDPHS_IRQn) once the device is open and attached. */
    AIC_REGS->AIC_SSR  = AIC_SSR_INTSEL((uint32_t) UDPHS_IRQn);
    AIC_REGS->AIC_IDCR = AIC_IDCR_Msk;

    __DSB();
    __enable_irq();
    __ISB();
```

The application enables it after open+attach:

```c
/* perf_log_sink_cdc.c, after USB_DEVICE_Open + EventHandlerSet + Attach */
(void) SYS_INT_SourceEnable(UDPHS_IRQn);
```

**Masking must be inside `AIC_INT_Initialize`.** A mask placed after the call returns (e.g. in `SYS_Initialize`) loses the race, because the storm starts inside `AIC_INT_Initialize`'s own `__enable_irq()`.

**Preferred upstream shape:** `AIC_INT_Initialize` should not blanket-enable sources and unmask IRQs while drivers are uninitialized. Either (a) register handlers/priorities but leave sources disabled, letting each driver `SYS_INT_SourceEnable` its own source when ready; or (b) at minimum expose a per-source "configure but don't enable" option. This mirrors the NVIC-family convention where the IRQ is enabled by the driver, not blanket-enabled at init.

### Fix 2 — UDPHS ISR must acknowledge on every exit path

In `F_DRV_USB_UDPHS_DEVICE_Tasks_ISR`, the no-client branch:

```c
if ((false == hDriver->isOpened) || (NULL == hDriver->pEventCallBack)) {
    /* No client can service this interrupt. On a level-sensitive controller
     * returning without acknowledging re-enters this ISR indefinitely.
     * Detach so an attached host stops driving bus resets, then mask and
     * clear so the request de-asserts. */
    usbID = hDriver->usbID;
    usbID->UDPHS_CTRL  |= UDPHS_CTRL_DETACH_Msk;
    usbID->UDPHS_IEN    = 0U;
    usbID->UDPHS_CLRINT = UDPHS_CLRINT_Msk;
    /* SYS_DEBUG_MESSAGE(...) */
}
```

### Fix 3 — `F_DRV_USB_UDPHS_DEVICE_Initialize` should finish detached

Move the detach after the Reset-IP toggle:

```c
/* Reset IP */
usbID->UDPHS_CTRL &= ~UDPHS_CTRL_EN_UDPHS_Msk;
usbID->UDPHS_CTRL |=  UDPHS_CTRL_EN_UDPHS_Msk;

/* Detach AFTER the reset toggle (which clears DETACH), so the controller
 * comes up disconnected until the app calls Attach. */
usbID->UDPHS_CTRL |= UDPHS_CTRL_DETACH_Msk;
usbID->UDPHS_CTRL |= UDPHS_CTRL_PULLD_DIS_Msk;
```

## 8. Reproduction

1. Harmony USB device (any function) on an AIC part, interrupt mode.
2. Application opens/attaches the device from a task after early boot (not in `SYS_Initialize`).
3. Attach a USB host to the device port, then power-on / reset.
4. Observe the hang in `SYS_Initialize` (scheduler never starts; CPU pinned in the UDPHS ISR).

Baseline workaround (no code): attach the host only *after* the application is up.

## 9. Verification

With Fix 1 (+2/+3), the board boots first-try with the host attached at power-on, deterministically across repeated cold power cycles, and the CDC device enumerates normally once the app enables the source post-attach.
