/*
 * Copyright (C) 2009 - 2019 Xilinx, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "xparameters.h"
#include "xil_printf.h"

#include "lwip/err.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

#include "xil_io.h"
#include "xil_types.h"
#include "xaxidma.h"
#include "xil_cache.h"

#ifdef XPAR_XAXIDMA_0_BASEADDR
#define DMA_BASEADDR XPAR_XAXIDMA_0_BASEADDR
#else
#define DMA_BASEADDR XPAR_AXI_DMA_0_BASEADDR
#endif

#ifdef XPAR_AXI_GPIO_RLE_STATUS_BASEADDR
#define RLE_GPIO_BASEADDR XPAR_AXI_GPIO_RLE_STATUS_BASEADDR
#elif defined(XPAR_AXI_GPIO_0_BASEADDR)
#define RLE_GPIO_BASEADDR XPAR_AXI_GPIO_0_BASEADDR
#elif defined(XPAR_XGPIO_0_BASEADDR)
#define RLE_GPIO_BASEADDR XPAR_XGPIO_0_BASEADDR
#else
#error "Cannot find AXI GPIO base address macro for RLE status"
#endif

#define RLE_GPIO_CH1_DATA_OFFSET 0x00U
#define RLE_GPIO_CH2_DATA_OFFSET 0x08U

#define TX_BUFFER_SIZE      65536
#define RX_BUFFER_SIZE      65536
#define RLE_ACCUM_TARGET_SIZE   (64U * 1024U)
#define TCP_SEND_CHUNK_SIZE     1460U
#define RESPONSE_BUFFER_SIZE    (1024U * 1024U)
#define LARGE_BENCHMARK_MODE    1
#define TIMEOUT_LIMIT       100000000

#define GLOBAL_TIMER_BASEADDR   0xF8F00200U
#define GLOBAL_TIMER_COUNTER_LO 0x00U
#define GLOBAL_TIMER_COUNTER_HI 0x04U
#define GLOBAL_TIMER_CONTROL    0x08U

#define GLOBAL_TIMER_FREQ_HZ 333333333ULL

static XAxiDma AxiDma;

static uint8_t tx_buffer[TX_BUFFER_SIZE] __attribute__((aligned(64)));
static uint8_t rx_buffer[RX_BUFFER_SIZE] __attribute__((aligned(64)));
static uint8_t sw_ref_buffer[RX_BUFFER_SIZE] __attribute__((aligned(64)));
static uint32_t debug_block_id = 0;
static uint8_t response_buffer[RESPONSE_BUFFER_SIZE] __attribute__((aligned(64)));
static uint8_t sw_response_buffer[RESPONSE_BUFFER_SIZE] __attribute__((aligned(64)));
static uint32_t sw_response_len = 0;
static uint32_t rle_accum_len = 0;

static uint32_t response_len = 0;
static uint32_t response_queued = 0;
static uint32_t response_acked = 0;
static uint8_t input_closed = 0;

static uint32_t bench_dma_calls = 0;
static uint32_t bench_input_bytes = 0;
static uint32_t bench_compressed_bytes = 0;
static uint32_t bench_hw_time_us = 0;
static uint32_t fnv1a32(uint8_t *buf, uint32_t len);
static uint32_t fnv1a32_update(uint32_t h, uint8_t *buf, uint32_t len);

static uint32_t bench_hash = 2166136261U;

static uint32_t debug_fail_count = 0;
static uint32_t first_fail_block = 0;
static uint32_t first_fail_hw_len = 0;
static uint32_t first_fail_sw_len = 0;

static int wait_dma_done(void)
{
    int timeout = TIMEOUT_LIMIT;

    while (XAxiDma_Busy(&AxiDma, XAXIDMA_DMA_TO_DEVICE)) {
        timeout--;
        if (timeout <= 0) {
            xil_printf("ERROR: MM2S timeout\r\n");
            xil_printf("MM2S busy = %d\r\n", XAxiDma_Busy(&AxiDma, XAXIDMA_DMA_TO_DEVICE));
            xil_printf("S2MM busy = %d\r\n", XAxiDma_Busy(&AxiDma, XAXIDMA_DEVICE_TO_DMA));
            
            return -1;
        }
    }

    timeout = TIMEOUT_LIMIT;

    while (XAxiDma_Busy(&AxiDma, XAXIDMA_DEVICE_TO_DMA)) {
        timeout--;
        if (timeout <= 0) {
            xil_printf("ERROR: S2MM timeout\r\n");
            xil_printf("MM2S busy = %d\r\n", XAxiDma_Busy(&AxiDma, XAXIDMA_DMA_TO_DEVICE));
            xil_printf("S2MM busy = %d\r\n", XAxiDma_Busy(&AxiDma, XAXIDMA_DEVICE_TO_DMA));
            return -1;
        }
    }

    return 0;
}

static uint32_t rle_hw_read_compressed_size(void)
{
    return Xil_In32(RLE_GPIO_BASEADDR + RLE_GPIO_CH1_DATA_OFFSET);
}

static uint32_t rle_hw_read_done(void)
{
    return Xil_In32(RLE_GPIO_BASEADDR + RLE_GPIO_CH2_DATA_OFFSET) & 0x1U;
}

static void timer_init(void)
{
    Xil_Out32(GLOBAL_TIMER_BASEADDR + GLOBAL_TIMER_CONTROL, 0x00000001U);
}

static uint64_t timer_read(void)
{
    uint32_t hi1, hi2, lo;

    do {
        hi1 = Xil_In32(GLOBAL_TIMER_BASEADDR + GLOBAL_TIMER_COUNTER_HI);
        lo  = Xil_In32(GLOBAL_TIMER_BASEADDR + GLOBAL_TIMER_COUNTER_LO);
        hi2 = Xil_In32(GLOBAL_TIMER_BASEADDR + GLOBAL_TIMER_COUNTER_HI);
    } while (hi1 != hi2);

    return (((uint64_t)hi1) << 32) | lo;
}

static uint32_t elapsed_us(uint64_t start, uint64_t end)
{
    uint64_t diff = end - start;
    return (uint32_t)((diff * 1000000ULL) / GLOBAL_TIMER_FREQ_HZ);
}

static int compress_chunk_dma(uint8_t *input_buf,
                              int input_len,
                              uint8_t *output_buf,
                              int output_capacity,
                              uint32_t *compressed_len,
                              uint32_t *hw_time_us)
{
    int Status;
    uint64_t hw_start, hw_end;
    uint32_t hw_compressed_size;
    uint32_t hw_done;

    if (input_buf == 0 || output_buf == 0 || compressed_len == 0 || hw_time_us == 0) {
        xil_printf("ERROR: compress_chunk_dma got null pointer\r\n");
        return -1;
    }

    if (input_len <= 0) {
        xil_printf("ERROR: compress_chunk_dma invalid input_len = %d\r\n", input_len);
        return -1;
    }

    if (output_capacity <= 0) {
        xil_printf("ERROR: compress_chunk_dma invalid output_capacity = %d\r\n", output_capacity);
        return -1;
    }

    *compressed_len = 0;
    *hw_time_us = 0;

    memset(output_buf, 0, output_capacity);

    Xil_DCacheFlushRange((UINTPTR)input_buf, input_len);
    Xil_DCacheFlushRange((UINTPTR)output_buf, output_capacity);
    Xil_DCacheInvalidateRange((UINTPTR)output_buf, output_capacity);

    hw_start = timer_read();

    Status = XAxiDma_SimpleTransfer(
        &AxiDma,
        (UINTPTR)output_buf,
        output_capacity,
        XAXIDMA_DEVICE_TO_DMA
    );

    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: S2MM transfer failed: %d\r\n", Status);
        return -1;
    }

    Status = XAxiDma_SimpleTransfer(
        &AxiDma,
        (UINTPTR)input_buf,
        input_len,
        XAXIDMA_DMA_TO_DEVICE
    );

    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: MM2S transfer failed: %d\r\n", Status);
        return -1;
    }

    if (wait_dma_done() != 0) {
        Xil_DCacheInvalidateRange((UINTPTR)output_buf, output_capacity);
        xil_printf("ERROR: DMA did not complete inside compress_chunk_dma\r\n");
        return -1;
    }

    hw_end = timer_read();
    *hw_time_us = elapsed_us(hw_start, hw_end);

    Xil_DCacheInvalidateRange((UINTPTR)output_buf, output_capacity);

    hw_compressed_size = rle_hw_read_compressed_size();
    hw_done = rle_hw_read_done();

    if (hw_done != 1U) {
        xil_printf("ERROR: RLE HW done is not 1 after DMA completion\r\n");
        return -1;
    }

    if (hw_compressed_size == 0U) {
        xil_printf("ERROR: RLE HW compressed_size is 0\r\n");
        return -1;
    }

    if (hw_compressed_size > (uint32_t)output_capacity) {
        xil_printf("ERROR: RLE HW compressed_size exceeds output buffer. HW=%u capacity=%u\r\n",
                   (unsigned int)hw_compressed_size,
                   (unsigned int)output_capacity);
        return -1;
    }

    *compressed_len = hw_compressed_size;

    return 0;
}


static int rle_dma_init(void)
{
    int Status;
    XAxiDma_Config *CfgPtr;

    CfgPtr = XAxiDma_LookupConfig(DMA_BASEADDR);
    if (!CfgPtr) {
        xil_printf("ERROR: XAxiDma_LookupConfig failed\r\n");
        return -1;
    }

    Status = XAxiDma_CfgInitialize(&AxiDma, CfgPtr);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: XAxiDma_CfgInitialize failed: %d\r\n", Status);
        return -1;
    }

    if (XAxiDma_HasSg(&AxiDma)) {
        xil_printf("ERROR: DMA is in Scatter-Gather mode\r\n");
        return -1;
    }

    XAxiDma_IntrDisable(&AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
    XAxiDma_IntrDisable(&AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);

    xil_printf("RLE DMA initialized\r\n");
    xil_printf("DMA baseaddr = 0x%08x\r\n", DMA_BASEADDR);
    xil_printf("RLE GPIO baseaddr = 0x%08x\r\n", RLE_GPIO_BASEADDR);

    return 0;
}

static err_t tcp_try_send_response(struct tcp_pcb *tpcb)
{
    err_t err;

    while (response_queued < response_len) {
        u16_t sndbuf = tcp_sndbuf(tpcb);

        if (sndbuf == 0) {
            break;
        }

        uint32_t remaining = response_len - response_queued;
        uint32_t chunk_len = remaining;

        if (chunk_len > sndbuf) {
            chunk_len = sndbuf;
        }

        if (chunk_len > TCP_SEND_CHUNK_SIZE) {
            chunk_len = TCP_SEND_CHUNK_SIZE;
        }

        /*
         * Important:
         * We use TCP_WRITE no-copy mode, so lwIP/EMAC may send directly
         * from response_buffer. Flush the exact payload range so DMA sees
         * the correct bytes in DDR, not stale cache data.
         */
        Xil_DCacheFlushRange((UINTPTR)&response_buffer[response_queued],
                             chunk_len);

        err = tcp_write(tpcb,
                        &response_buffer[response_queued],
                        (u16_t)chunk_len,
                        0);

        if (err == ERR_MEM) {
            /*
            * Not fatal. TCP queue/pbuf memory is temporarily full.
            * We will continue when tcp_sent_callback() is called after ACKs.
            */
            break;
        }

        if (err != ERR_OK) {
            xil_printf("ERROR: tcp_write failed: %d queued=%u/%u\r\n",
                    err,
                    (unsigned int)response_queued,
                    (unsigned int)response_len);
            return err;
        }

        response_queued += chunk_len;
    }

    tcp_output(tpcb);

    return ERR_OK;
}


static err_t tcp_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    (void)arg;

    response_acked += len;

    tcp_try_send_response(tpcb);

    if (input_closed &&
        response_acked >= response_len &&
        response_queued >= response_len) {

        uint32_t response_hash_after = fnv1a32(response_buffer, response_len);

        xil_printf("RESPONSE HASH after TCP send: len=%u fnv1a=0x%08x\r\n",
                (unsigned int)response_len,
                (unsigned int)response_hash_after);

        xil_printf("TCP response sent completely: %u bytes\r\n",
                   (unsigned int)response_len);

        tcp_sent(tpcb, NULL);
        tcp_recv(tpcb, NULL);
        tcp_close(tpcb);
    }

    return ERR_OK;
}

static void sw_emit_run(uint8_t *out, uint32_t *out_len, uint8_t base, uint32_t count)
{
    if (count == 0) {
        return;
    }

    if (count > 5U) {
        out[*out_len + 0] = (uint8_t)(count & 0xFFU);
        out[*out_len + 1] = (uint8_t)((count >> 8) & 0xFFU);
        out[*out_len + 2] = (uint8_t)((count >> 16) & 0xFFU);
        out[*out_len + 3] = (uint8_t)((count >> 24) & 0xFFU);
        out[*out_len + 4] = base;
        *out_len += 5U;
    } else {
        for (uint32_t i = 0; i < count; i++) {
            out[*out_len] = base;
            *out_len += 1U;
        }
    }
}

static int is_base(uint8_t c)
{
    return (c == 'A' || c == 'C' || c == 'G' || c == 'T' || c == 'N');
}

static uint32_t sw_compress_chunk(uint8_t *input, uint32_t input_len, uint8_t *out)
{
    uint8_t in_header = 0;
    uint8_t prev = 0;
    uint32_t count = 0;
    uint32_t out_len = 0;

    for (uint32_t i = 0; i < input_len; i++) {
        uint8_t c = input[i];

        if (in_header) {
            if (c == '\n' || c == '\r') {
                in_header = 0;
            }
            continue;
        }

        if (c == '>') {
            in_header = 1;
            continue;
        }

        if (c == '\n' || c == '\r') {
            continue;
        }

        if (c >= 'a' && c <= 'z') {
            c = (uint8_t)(c - 32U);
        }

        if (!is_base(c)) {
            continue;
        }

        if (count == 0) {
            prev = c;
            count = 1;
        } else if (c == prev) {
            count++;
        } else {
            sw_emit_run(out, &out_len, prev, count);
            prev = c;
            count = 1;
        }
    }

    sw_emit_run(out, &out_len, prev, count);

    return out_len;
}

static void debug_compare_hw_sw(uint32_t block_id,
                                uint8_t *hw,
                                uint32_t hw_len,
                                uint8_t *sw,
                                uint32_t sw_len)
{
    if (hw_len != sw_len) {
    debug_fail_count++;

    if (first_fail_block == 0U) {
        first_fail_block = block_id;
        first_fail_hw_len = hw_len;
        first_fail_sw_len = sw_len;

        xil_printf("FIRST DEBUG FAIL: block=%u length mismatch HW=%u SW=%u\r\n",
                   (unsigned int)block_id,
                   (unsigned int)hw_len,
                   (unsigned int)sw_len);

        uint32_t min_len = (hw_len < sw_len) ? hw_len : sw_len;

        for (uint32_t i = 0; i < min_len; i++) {
            if (hw[i] != sw[i]) {
                xil_printf("FIRST BYTE MISMATCH: offset=%u HW=0x%02x SW=0x%02x\r\n",
                           (unsigned int)i,
                           (unsigned int)hw[i],
                           (unsigned int)sw[i]);

                uint32_t start = (i > 16U) ? (i - 16U) : 0U;
                uint32_t end = i + 32U;
                if (end > min_len) {
                    end = min_len;
                }

                xil_printf("HW around: ");
                for (uint32_t k = start; k < end; k++) {
                    xil_printf("%02x ", hw[k]);
                }
                xil_printf("\r\n");

                xil_printf("SW around: ");
                for (uint32_t k = start; k < end; k++) {
                    xil_printf("%02x ", sw[k]);
                }
                xil_printf("\r\n");

                break;
            }
        }
    }

    return;
    }

    for (uint32_t i = 0; i < hw_len; i++) {
        if (hw[i] != sw[i]) {
    debug_fail_count++;

    if (first_fail_block == 0U) {
        first_fail_block = block_id;
        first_fail_hw_len = hw_len;
        first_fail_sw_len = sw_len;

        xil_printf("FIRST DEBUG FAIL: block=%u offset=%u HW=0x%02x SW=0x%02x\r\n",
                   (unsigned int)block_id,
                   (unsigned int)i,
                   (unsigned int)hw[i],
                   (unsigned int)sw[i]);

        uint32_t start = (i > 16U) ? (i - 16U) : 0U;
        uint32_t end = i + 32U;
        if (end > hw_len) {
            end = hw_len;
        }

        xil_printf("HW around: ");
        for (uint32_t k = start; k < end; k++) {
            xil_printf("%02x ", hw[k]);
        }
        xil_printf("\r\n");

        xil_printf("SW around: ");
        for (uint32_t k = start; k < end; k++) {
            xil_printf("%02x ", sw[k]);
        }
        xil_printf("\r\n");
    }

    return;
    }
        
       
    }

#if LARGE_BENCHMARK_MODE == 0
    xil_printf("DEBUG PASS block=%u len=%u\r\n",
               (unsigned int)block_id,
               (unsigned int)hw_len);
#endif
}

static err_t flush_rle_accum_buffer(struct tcp_pcb *tpcb)
{
    uint32_t compressed_len = 0;
    uint32_t hw_time_us = 0;
    err_t err;

    if (rle_accum_len == 0) {
        return ERR_OK;
    }

    if (compress_chunk_dma(tx_buffer,
                           rle_accum_len,
                           rx_buffer,
                           RX_BUFFER_SIZE,
                           &compressed_len,
                           &hw_time_us) != 0) {
        xil_printf("ERROR: compress_chunk_dma failed for buffered input\r\n");
        rle_accum_len = 0;
        return ERR_OK;
    }

    bench_dma_calls++;
    bench_input_bytes += rle_accum_len;
    bench_compressed_bytes += compressed_len;
    bench_hw_time_us += hw_time_us;

    #if LARGE_BENCHMARK_MODE == 0
    xil_printf("BUFFER=%u bytes  compressed=%u bytes  hw_time=%u us\r\n",
               (unsigned int)rle_accum_len,
               (unsigned int)compressed_len,
               (unsigned int)hw_time_us);
    #endif
    debug_block_id++;

    uint32_t sw_len = sw_compress_chunk(tx_buffer, rle_accum_len, sw_ref_buffer);

    debug_compare_hw_sw(debug_block_id,
                        rx_buffer,
                        compressed_len,
                        sw_ref_buffer,
                        sw_len);
    
    bench_hash = fnv1a32_update(bench_hash, rx_buffer, compressed_len);

    #if LARGE_BENCHMARK_MODE == 1
        rle_accum_len = 0;
        return ERR_OK;
    #endif

    if ((response_len + compressed_len) > RESPONSE_BUFFER_SIZE) {
    xil_printf("ERROR: response_buffer overflow. response=%u compressed=%u capacity=%u\r\n",
               (unsigned int)response_len,
               (unsigned int)compressed_len,
               (unsigned int)RESPONSE_BUFFER_SIZE);

    rle_accum_len = 0;
    return ERR_MEM;
    }

    if ((sw_response_len + sw_len) > RESPONSE_BUFFER_SIZE) {
    xil_printf("ERROR: sw_response_buffer overflow. sw_response=%u sw_len=%u capacity=%u\r\n",
               (unsigned int)sw_response_len,
               (unsigned int)sw_len,
               (unsigned int)RESPONSE_BUFFER_SIZE);

    rle_accum_len = 0;
    return ERR_MEM;
    }

    memcpy(&response_buffer[response_len], rx_buffer, compressed_len);
    memcpy(&sw_response_buffer[sw_response_len], sw_ref_buffer, sw_len);

    response_len += compressed_len;
    sw_response_len += sw_len;

    rle_accum_len = 0;

    return ERR_OK;

   
}

static void debug_compare_full_response(void)
{
    if (response_len != sw_response_len) {
        xil_printf("FULL RESPONSE FAIL: length mismatch response=%u sw=%u\r\n",
                   (unsigned int)response_len,
                   (unsigned int)sw_response_len);
        return;
    }

    for (uint32_t i = 0; i < response_len; i++) {
        if (response_buffer[i] != sw_response_buffer[i]) {
            xil_printf("FULL RESPONSE FAIL: offset=%u response=0x%02x sw=0x%02x\r\n",
                       (unsigned int)i,
                       (unsigned int)response_buffer[i],
                       (unsigned int)sw_response_buffer[i]);

            uint32_t start = (i > 16U) ? (i - 16U) : 0U;
            uint32_t end = i + 32U;
            if (end > response_len) {
                end = response_len;
            }

            xil_printf("RESP around: ");
            for (uint32_t k = start; k < end; k++) {
                xil_printf("%02x ", response_buffer[k]);
            }
            xil_printf("\r\n");

            xil_printf("SW around: ");
            for (uint32_t k = start; k < end; k++) {
                xil_printf("%02x ", sw_response_buffer[k]);
            }
            xil_printf("\r\n");

            return;
        }
    }

    xil_printf("FULL RESPONSE PASS: %u bytes\r\n",
               (unsigned int)response_len);
}

static uint32_t fnv1a32(uint8_t *buf, uint32_t len)
{
    uint32_t h = 2166136261U;

    for (uint32_t i = 0; i < len; i++) {
        h ^= buf[i];
        h *= 16777619U;
    }

    return h;
}

static uint32_t fnv1a32_update(uint32_t h, uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        h ^= buf[i];
        h *= 16777619U;
    }

    return h;
}

err_t recv_callback(void *arg, struct tcp_pcb *tpcb,
                    struct pbuf *p, err_t err)
{
    uint32_t copied = 0;

    (void)arg;
    (void)err;

    if (!p) {
        input_closed = 1;

        flush_rle_accum_buffer(tpcb);

        xil_printf("BENCH BUFFER: dma_calls=%u  input=%u bytes  compressed=%u bytes  total_hw_time=%u us\r\n",
                (unsigned int)bench_dma_calls,
                (unsigned int)bench_input_bytes,
                (unsigned int)bench_compressed_bytes,
                (unsigned int)bench_hw_time_us);
        
        #if LARGE_BENCHMARK_MODE == 1

            xil_printf("LARGE BENCHMARK RESULT\r\n");
            xil_printf("buffer_size=%u bytes\r\n",
                    (unsigned int)RLE_ACCUM_TARGET_SIZE);
            xil_printf("dma_calls=%u\r\n",
                    (unsigned int)bench_dma_calls);
            xil_printf("input_bytes=%u\r\n",
                    (unsigned int)bench_input_bytes);
            xil_printf("compressed_bytes=%u\r\n",
                    (unsigned int)bench_compressed_bytes);
            xil_printf("total_hw_time_us=%u\r\n",
                    (unsigned int)bench_hw_time_us);
            xil_printf("compressed_hash_fnv1a=0x%08x\r\n",
                    (unsigned int)bench_hash);
            xil_printf("debug_fail_count=%u\r\n",
                    (unsigned int)debug_fail_count);
            xil_printf("first_fail_block=%u HW_len=%u SW_len=%u\r\n",
                    (unsigned int)first_fail_block,
                    (unsigned int)first_fail_hw_len,
                    (unsigned int)first_fail_sw_len);

            tcp_recv(tpcb, NULL);
            tcp_sent(tpcb, NULL);
            tcp_close(tpcb);

            return ERR_OK;

        #endif
        
        debug_compare_full_response();

        uint32_t response_hash = fnv1a32(response_buffer, response_len);

        xil_printf("RESPONSE HASH before TCP send: len=%u fnv1a=0x%08x\r\n",
                (unsigned int)response_len,
                (unsigned int)response_hash);

        xil_printf("Starting TCP response send: %u bytes\r\n",
                (unsigned int)response_len);

        tcp_recv(tpcb, NULL);

        if (response_len == 0) {
            tcp_close(tpcb);
            return ERR_OK;
        }

        tcp_try_send_response(tpcb);

        return ERR_OK;
    }

    

    tcp_recved(tpcb, p->tot_len);

    while (copied < p->tot_len) {
        uint32_t space;
        uint32_t remaining;
        uint32_t copy_len;

        if (rle_accum_len >= RLE_ACCUM_TARGET_SIZE) {
            if (flush_rle_accum_buffer(tpcb) != ERR_OK) {
                pbuf_free(p);
                return ERR_MEM;
            }
        }

        space = RLE_ACCUM_TARGET_SIZE - rle_accum_len;
        remaining = p->tot_len - copied;
        copy_len = remaining;

        if (copy_len > space) {
            copy_len = space;
        }

        pbuf_copy_partial(p,
                          &tx_buffer[rle_accum_len],
                          (u16_t)copy_len,
                          (u16_t)copied);

        rle_accum_len += copy_len;
        copied += copy_len;

        if (rle_accum_len >= RLE_ACCUM_TARGET_SIZE) {
            if (flush_rle_accum_buffer(tpcb) != ERR_OK) {
                pbuf_free(p);
                return ERR_MEM;
            }
        }
    }

    pbuf_free(p);

    return ERR_OK;
}

int transfer_data()
{
    return 0;
}

void print_app_header()
{
#if (LWIP_IPV6==0)
    xil_printf("\n\r\n\r-----lwIP TCP echo server ------\n\r");
#else
    xil_printf("\n\r\n\r-----lwIPv6 TCP echo server ------\n\r");
#endif
    xil_printf("TCP packets sent to port 7 will be echoed back\n\r");
}

err_t accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    static int connection = 1;

    (void)arg;
    (void)err;

    rle_accum_len = 0;

    debug_block_id = 0;

    sw_response_len = 0;

    response_len = 0;
    response_queued = 0;
    response_acked = 0;
    input_closed = 0;

    bench_dma_calls = 0;
    bench_input_bytes = 0;
    bench_compressed_bytes = 0;
    bench_hw_time_us = 0;

    bench_hash = 2166136261U;

    debug_fail_count = 0;
    first_fail_block = 0;
    first_fail_hw_len = 0;
    first_fail_sw_len = 0;

    tcp_recv(newpcb, recv_callback);
    tcp_sent(newpcb, tcp_sent_callback);

    tcp_arg(newpcb, (void*)(UINTPTR)connection);

    connection++;

    return ERR_OK;
}


int start_application()
{
	struct tcp_pcb *pcb;
	err_t err;
	unsigned port = 7;

    timer_init();

    if (rle_dma_init() != 0) {
        xil_printf("ERROR: RLE DMA init failed\r\n");
        return -10;
    }

	/* create new TCP PCB structure */
	pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
	if (!pcb) {
		xil_printf("Error creating PCB. Out of Memory\n\r");
		return -1;
	}

	/* bind to specified @port */
	err = tcp_bind(pcb, IP_ANY_TYPE, port);
	if (err != ERR_OK) {
		xil_printf("Unable to bind to port %d: err = %d\n\r", port, err);
		return -2;
	}

	/* we do not need any arguments to callback functions */
	tcp_arg(pcb, NULL);

	/* listen for connections */
	pcb = tcp_listen(pcb);
	if (!pcb) {
		xil_printf("Out of memory while tcp_listen\n\r");
		return -3;
	}

	/* specify callback to use for incoming connections */
	tcp_accept(pcb, accept_callback);

	xil_printf("TCP echo server started @ port %d\n\r", port);

	return 0;
}
