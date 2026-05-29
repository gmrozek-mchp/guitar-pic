# UDPHS device driver: queued IRPs are accepted but never armed for DMA

**Affected:** Harmony USB v3.16.0, file `driver/usb/udphs/src/drv_usb_udphs_device.c`.
**Tested on:** SAM9X75. The defect is in shared driver code that runs on every UDPHS-based part, so it likely affects SAM9X60, SAMA5D2, SAM9X25, and any other UDPHS device using this driver.
**Symptom severity:** Per-instance `queueSizeWrite > 1` is behaviorally indistinguishable from `queueSizeWrite = 1`. Bulk-IN throughput is capped at the application's round-trip-per-write rate even when the lower CDC and driver layers are configured for multi-IRP pipelining.

## Summary

The stock `drv_usb_udphs_device.c` only programs the UDPHS DMA channel in `DRV_USB_UDPHS_DEVICE_IRPSubmit`'s queue-empty branch. When an IRP is submitted to a non-empty endpoint queue, it is appended to the linked list (via `iterator->next = irp_t`) but no DMA programming follows. The IRP completion ISR (`F_DRV_USB_UDPHS_DEVICE_Tasks_ISR_DMA`) advances the queue head with `endpointObj->irpQueue = irp->next` and fires the application callback, but does not arm DMA for the new head. The same omission exists in the ZLP-completion path inside `F_DRV_USB_UDPHS_DEVICE_Tasks_ISR`.

Result: only the first IRP of any submission batch ever transmits. Subsequent IRPs sit at `STATUS_PENDING` in the queue indefinitely, eventually accumulating until `currentQSizeWrite >= queueSizeWrite` rejects all further submissions with `USB_DEVICE_CDC_RESULT_ERROR_TRANSFER_QUEUE_FULL`. The application sees the first write succeed and complete, then writes silently stall.

## Reproduction

1. Configure a USB device CDC instance with `queueSizeWrite = 3` (and `USB_DEVICE_CDC_QUEUE_DEPTH_COMBINED` ≥ 4 to allow it).
2. Submit three back-to-back `USB_DEVICE_CDC_Write` calls from a task with payloads ≥ 1 KB each.
3. Observe via the CDC class event handler: only one `USB_DEVICE_CDC_EVENT_WRITE_COMPLETE` event fires (for the first transfer). The other two transfers never complete; the IRPs stay `STATUS_PENDING` in `endpointObj->irpQueue`.
4. From the host (USB 2.0 HS bulk-IN): only the first chunk's bytes arrive on the wire; subsequent IN tokens get NAK'd because the controller has nothing buffered.

## Root-cause analysis

`DRV_USB_UDPHS_DEVICE_IRPSubmit` for non-zero, DMA-capable endpoints has two paths:

**Queue empty** (around `if (endpointObj->irpQueue == NULL)`, line 1604 in v3.16.0):
- Sets `irp_t->status = USB_DEVICE_IRP_STATUS_IN_PROGRESS`.
- Programs the DMA descriptor chain in `endpointObj->dmaTransferDescriptor[]`.
- Cleans D-cache for the IRP buffer (`SYS_CACHE_CleanDCache_by_Addr`).
- Writes `UDPHS_DMANXTDSC` with the descriptor chain start, then `UDPHS_DMACONTROL = LDNXT_DSC` to load the descriptor and start the channel.
- Enables the DMA interrupt for the endpoint via `usbID->UDPHS_IEN |= ...`.

**Queue non-empty** (around lines 2131-2144 in v3.16.0):
- Walks the linked list to the tail with `iterator->next` chasing.
- Appends `irp_t` with `irp_t->status = USB_DEVICE_IRP_STATUS_PENDING` and `iterator->next = irp_t`.
- **Returns without touching the DMA hardware.**

`F_DRV_USB_UDPHS_DEVICE_Tasks_ISR_DMA` on completion (lines 2475-2541 in v3.16.0):
- Reads `UDPHS_DMASTATUS`, marks the head IRP `STATUS_COMPLETED` once `nPendingBytes == 0`.
- Sets `endpointObj->irpQueue = irp->next` (advance queue head).
- Fires the application callback.
- **Returns without arming DMA for the new head.**

The same omission exists in the ZLP-completion path inside `F_DRV_USB_UDPHS_DEVICE_Tasks_ISR` (around line 3083 in v3.16.0 — DMA-capable endpoint, non-DMA interrupt indicating ZLP completion):
- Marks IRP `STATUS_COMPLETED`.
- Advances `endpointObj->irpQueue = irp->next`.
- Fires the application callback.
- **Returns without arming DMA for the new head.**

The IRP-queue *infrastructure* is fully built — linked-list management, status tracking, callback firing, and cancellation walks (`F_DRV_USB_UDPHS_DEVICE_IRPQueueFlush`, `DRV_USB_UDPHS_DEVICE_IRPCancel`, `DRV_USB_UDPHS_DEVICE_IRPCancelAll`) all handle multi-IRP queues correctly. The only missing piece is the post-completion arm in the two completion paths.

## Suggested fix

Additive change — does not refactor the existing inline DMA-program block in `IRPSubmit`. Adds a new static helper that mirrors that block (covering both `DEVICE_TO_HOST` and `HOST_TO_DEVICE` directions), and calls it from the two completion paths after each queue advance.

### Helper (place above `DRV_USB_UDPHS_DEVICE_IRPSubmit`)

```c
/* Program the UDPHS DMA channel for the IRP at the head of an endpoint's
 * queue. Called from the DMA and ZLP completion paths to start the next
 * pending IRP after the previous one has been retired and the queue head
 * has been advanced. Mirrors the DMA-program block in IRPSubmit's queue-
 * empty branch (non-zero endpoint, DMA-capable, non-ZLP path). Caller
 * must hold the driver mutex or be in ISR context. */
static void F_DRV_USB_UDPHS_DEVICE_ArmDmaForIrp(
    DRV_USB_UDPHS_OBJ *hDriver,
    DRV_USB_UDPHS_DEVICE_ENDPOINT_OBJ *endpointObj,
    USB_DEVICE_IRP_LOCAL *irp_t,
    uint8_t endpoint)
{
    udphs_registers_t *usbID = hDriver->usbID;
    uint32_t dmaEpIndex = (uint32_t)endpoint - 1U + M_DRV_UDPHS_DMA_OFFSET;
    uint32_t dmaMaxTransfer;
    uint32_t remainder_t;
    uint8_t *data;
    uint32_t i;

    if (irp_t->size == 0U)
    {
        /* Zero-size IRPs aren't handled by the DMA arm path. */
        return;
    }

    irp_t->status = USB_DEVICE_IRP_STATUS_IN_PROGRESS;

    __DSB();
    __ISB();

    dmaMaxTransfer = irp_t->size / (64U * 1024U);
    remainder_t    = irp_t->size % (64U * 1024U);
    if (remainder_t != 0U)
    {
        dmaMaxTransfer++;
    }

    if (dmaMaxTransfer > (uint32_t)DRV_USB_UDPHS_DMA_MAX_TRANSFER_SIZE)
    {
        /* Transfer too large to pipeline. Abort and fire callback so the
         * application sees the error rather than silently stalling the
         * queue. */
        irp_t->status = USB_DEVICE_IRP_STATUS_ABORTED;
        endpointObj->irpQueue = irp_t->next;
        if (irp_t->callback != NULL)
        {
            irp_t->callback((USB_DEVICE_IRP *)irp_t);
        }
        return;
    }

    if (endpointObj->endpointDirection == USB_DATA_DIRECTION_DEVICE_TO_HOST)
    {
        data = (uint8_t *)irp_t->data;
        SYS_CACHE_CleanDCache_by_Addr((uint32_t *)irp_t->data, (int32_t)irp_t->size);

        for (i = 0; i < dmaMaxTransfer; i++)
        {
            endpointObj->dmaTransferDescriptor[i].bufferAddress = (void *)&data[64U * 1024U * i];

            if (i == (dmaMaxTransfer - 1U))
            {
                endpointObj->dmaTransferDescriptor[i].nextDescriptorAddress = NULL;
                endpointObj->dmaTransferDescriptor[i].dmaControl =
                     (UDPHS_DMACONTROL_BUFF_LENGTH(remainder_t)
                    | UDPHS_DMACONTROL_END_B_EN_Msk
                    | UDPHS_DMACONTROL_END_BUFFIT_Msk
                    | UDPHS_DMACONTROL_CHANN_ENB_Msk);
            }
            else
            {
                endpointObj->dmaTransferDescriptor[i].nextDescriptorAddress =
                    (void *)&endpointObj->dmaTransferDescriptor[i + 1U];
                endpointObj->dmaTransferDescriptor[i].dmaControl =
                     (UDPHS_DMACONTROL_BUFF_LENGTH(0UL)
                    | UDPHS_DMACONTROL_LDNXT_DSC_Msk
                    | UDPHS_DMACONTROL_CHANN_ENB_Msk);
            }
        }

        SYS_CACHE_CleanDCache_by_Addr((uint32_t *)endpointObj, (int32_t)sizeof(endpointObj));

        usbID->UDPHS_DMA[dmaEpIndex].UDPHS_DMANXTDSC  = (uint32_t)endpointObj->dmaTransferDescriptor;
        usbID->UDPHS_DMA[dmaEpIndex].UDPHS_DMACONTROL = UDPHS_DMACONTROL_LDNXT_DSC_Msk;
        usbID->UDPHS_IEN |= (UDPHS_IEN_DMA_1_Msk << (endpoint - 1U));
    }
    else
    {
        /* HOST_TO_DEVICE — bulk-OUT receive path. */
        SYS_CACHE_InvalidateDCache_by_Addr((uint32_t *)irp_t->data, (int32_t)irp_t->size);
        data = (uint8_t *)irp_t->data;

        for (i = 0; i < dmaMaxTransfer; i++)
        {
            endpointObj->dmaTransferDescriptor[i].bufferAddress = (void *)&data[64U * 1024U * i];

            if (i == (dmaMaxTransfer - 1U))
            {
                endpointObj->dmaTransferDescriptor[i].nextDescriptorAddress = NULL;
                endpointObj->dmaTransferDescriptor[i].dmaControl =
                     (UDPHS_DMACONTROL_BUFF_LENGTH(remainder_t)
                    | UDPHS_DMACONTROL_END_TR_EN_Msk
                    | UDPHS_DMACONTROL_END_TR_IT_Msk
                    | UDPHS_DMACONTROL_END_B_EN_Msk
                    | UDPHS_DMACONTROL_END_BUFFIT_Msk
                    | UDPHS_DMACONTROL_CHANN_ENB_Msk);
            }
            else
            {
                endpointObj->dmaTransferDescriptor[i].nextDescriptorAddress =
                    (void *)&endpointObj->dmaTransferDescriptor[i + 1U];
                endpointObj->dmaTransferDescriptor[i].dmaControl =
                     (UDPHS_DMACONTROL_BUFF_LENGTH(0UL)
                    | UDPHS_DMACONTROL_END_TR_EN_Msk
                    | UDPHS_DMACONTROL_END_TR_IT_Msk
                    | UDPHS_DMACONTROL_LDNXT_DSC_Msk
                    | UDPHS_DMACONTROL_CHANN_ENB_Msk);
            }
        }

        SYS_CACHE_CleanDCache_by_Addr((uint32_t *)endpointObj, (int32_t)sizeof(endpointObj));

        usbID->UDPHS_IEN |= (UDPHS_IEN_DMA_1_Msk << (endpoint - 1U));
        usbID->UDPHS_DMA[dmaEpIndex].UDPHS_DMANXTDSC  = (uint32_t)endpointObj->dmaTransferDescriptor;
        usbID->UDPHS_DMA[dmaEpIndex].UDPHS_DMACONTROL = UDPHS_DMACONTROL_LDNXT_DSC_Msk;
    }
}
```

The helper body is a verbatim copy of the inline DMA-program block from `IRPSubmit` lines 1880-2008 in v3.16.0. The only added behavior is the size-too-large guard at the top: if a queued IRP exceeds `DRV_USB_UDPHS_DMA_MAX_TRANSFER_SIZE × 64 KB`, mark it `STATUS_ABORTED`, advance the queue, and fire the callback so the application sees an explicit error rather than a silent stall. The original inline block returns `USB_ERROR_PARAMETER_INVALID` from `IRPSubmit` for the same condition; in the completion-path context there's no return value to propagate, hence the abort-and-callback alternative.

### Call site 1 — `F_DRV_USB_UDPHS_DEVICE_Tasks_ISR_DMA` (around line 2530 in v3.16.0)

After the existing queue advance and callback dispatch, add an arm of the new head:

```c
            /* Callback */
            if (irp->nPendingBytes == 0U)
            {
                endpointObj->irpQueue = irp->next;
                if(irp->callback != NULL)
                {
                    irp->callback((USB_DEVICE_IRP *)irp);
                }

                /* Arm DMA for the next pending IRP, if any — the queue
                 * head was just advanced and without this it would sit
                 * at STATUS_PENDING indefinitely. */
                if (endpointObj->irpQueue != NULL)
                {
                    F_DRV_USB_UDPHS_DEVICE_ArmDmaForIrp(
                        hDriver, endpointObj, endpointObj->irpQueue, NumEndpoint);
                }
            }
```

### Call site 2 — ZLP-completion path inside `F_DRV_USB_UDPHS_DEVICE_Tasks_ISR` (around line 3099 in v3.16.0)

Same pattern. This branch is entered when a DMA-capable endpoint signals via the non-DMA interrupt path, which happens after a ZLP transmission completes:

```c
                                {
                                    irp->callback((USB_DEVICE_IRP *)irp);
                                }

                                /* Arm DMA for the next pending IRP, if any.
                                 * This branch fires after a ZLP completion
                                 * has retired the previous IRP. */
                                if (endpointObj->irpQueue != NULL)
                                {
                                    F_DRV_USB_UDPHS_DEVICE_ArmDmaForIrp(
                                        hDriver, endpointObj,
                                        endpointObj->irpQueue, eptIndex);
                                }
                            }
                            else
                            {
```

## Testing performed

Tested on SAM9X75 with a custom USB CDC ACM application writing telemetry over bulk-IN at 60 Hz frame rate, payload sizes 17-28 KB per write. Configuration:

- `USB_DEVICE_CDC_INSTANCES_NUMBER = 1`
- `queueSizeWrite = 3` (per-instance, in `USB_DEVICE_CDC_INIT.queueSizeWrite`)
- `USB_DEVICE_CDC_QUEUE_DEPTH_COMBINED = 5` (combined IRP pool size — sized to fit `queueSizeWrite (3) + queueSizeRead (1) + queueSizeSerialStateNotification (1)`)
- Application-side: 3-deep ring of staging buffers, counting semaphore for credit-based producer flow control.

**Before the fix:** Only the first write of each submission batch transmitted on the wire. Subsequent writes were accepted by `USB_DEVICE_CDC_Write` (which returned `USB_DEVICE_CDC_RESULT_OK`) but never reached `USB_DEVICE_CDC_EVENT_WRITE_COMPLETE`. The IRPs sat at `STATUS_PENDING` in `endpointObj->irpQueue`. Once `currentQSizeWrite >= queueSizeWrite`, all further `USB_DEVICE_CDC_Write` calls returned `TRANSFER_QUEUE_FULL`. Sustained throughput was limited to roughly 1 MB/s by the round-trip-per-write pattern (single-IRP latency: ~340 µs DMA + ~1.5 ms task wake/format/CRC + ~30 µs submit ≈ 1.9 ms cycle, ~10 KB/cycle).

**After the fix:** All three queued IRPs transmit in submission order. `USB_DEVICE_CDC_EVENT_WRITE_COMPLETE` fires once per submission. Sustained throughput on hardware: 2.77 MB/s (60 Hz × ~46 KB/frame strip telemetry) with 0 drops at the application sink across 20+ second captures. CPU and wire both have headroom remaining. Time from completion ISR to next-IRP DMA start is bounded by the ISR work (~5 µs), enabling the wire to stay continuously busy.

## What we did NOT test

- **Multi-instance use** (we have one CDC instance). The IRP pool is shared across instances; a multi-instance configuration may surface different timing.
- **Bulk-OUT pipelining** — `queueSizeRead = 1` in our config, so the helper's `HOST_TO_DEVICE` branch was symmetric for completeness but never exercised under load. The branch mirrors the existing inline `HOST_TO_DEVICE` block in `IRPSubmit` byte for byte.
- **Suspend / resume mid-pipeline** — we don't currently exercise USB-suspend scenarios.
- **Other UDPHS-based parts** (SAM9X60, SAMA5D2, etc.) — we only have SAM9X75 hardware. The defect is in shared driver code so a fix there should benefit all UDPHS parts equally, but each has its own validation surface.
- **Endpoint cancellation under multi-IRP load** — the existing `IRPCancel` / `IRPCancelAll` / `IRPQueueFlush` paths walk the linked list correctly with multiple in-flight IRPs (verified by inspection), but we didn't stress them with cancellations during heavy writes.

## Notes for the maintainer

- The fix is **additive** (new static helper + two new call sites). The existing inline DMA-program block in `IRPSubmit` is untouched — refactoring `IRPSubmit` to call the helper as well would be a cleaner final state but introduces regression risk in a working code path; we left that for the maintainer's discretion.
- The helper is a near-verbatim copy of the inline block. If `IRPSubmit`'s DMA-program logic ever changes (e.g., to handle a new descriptor format), both copies will need to track each other. Hence the recommendation to consider unifying them in a follow-up.
- The cancellation paths (`IRPCancel`, `IRPCancelAll`, `IRPQueueFlush`) already handle multi-IRP queues correctly — no changes needed.

## Contact

Greg Mrozek, [greg.mrozek@microchip.com](mailto:greg.mrozek@microchip.com)
