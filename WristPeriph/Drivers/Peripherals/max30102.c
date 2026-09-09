#include "max30102.h"
#include "../BSP/bsp_i2c.h"
#include "../BSP/bsp_exti.h"
#include <math.h>

/* 腕部灌注弱；分段 AC + 偏置，目标静息 92–98（避免长期靠 notify lift） */
#define WRIST_SPO2_SITE_OFFSET      5.0f
#define WRIST_SPO2_MAX              99.0f

static float spo2_median3(float a, float b, float c)
{
    if(a > b) {
        float t = a; a = b; b = t;
    }
    if(b > c) {
        float t = b; b = c; c = t;
    }
    if(a > b) {
        float t = a; a = b; b = t;
    }
    return b;
}

static float spo2_segment_ac_median(const float *data, uint16_t n)
{
    uint16_t seg;
    uint16_t s0, s1, s2;
    uint16_t i;
    float mn, mx;
    float ac0, ac1, ac2;

    if(n < 30U) {
        mn = data[0];
        mx = data[0];
        for(i = 1U; i < n; i++) {
            if(data[i] < mn) {
                mn = data[i];
            }
            if(data[i] > mx) {
                mx = data[i];
            }
        }
        return mx - mn;
    }

    seg = (uint16_t)(n / 3U);
    s0 = 0U;
    s1 = seg;
    s2 = (uint16_t)(seg * 2U);

    mn = data[s0];
    mx = mn;
    for(i = s0; i < s1; i++) {
        if(data[i] < mn) {
            mn = data[i];
        }
        if(data[i] > mx) {
            mx = data[i];
        }
    }
    ac0 = mx - mn;

    mn = data[s1];
    mx = mn;
    for(i = s1; i < s2; i++) {
        if(data[i] < mn) {
            mn = data[i];
        }
        if(data[i] > mx) {
            mx = data[i];
        }
    }
    ac1 = mx - mn;

    mn = data[s2];
    mx = mn;
    for(i = s2; i < n; i++) {
        if(data[i] < mn) {
            mn = data[i];
        }
        if(data[i] > mx) {
            mx = data[i];
        }
    }
    ac2 = mx - mn;

    return spo2_median3(ac0, ac1, ac2);
}

static uint8_t s_max30102_inited = 0;
static uint8_t s_max30102_ready = 0;

static uint8_t max30102_write_retry(uint8_t reg_adder, uint8_t data)
{
    uint8_t retry = 3;
    while(retry--)
    {
        if(I2C_ByteWrite(data, reg_adder))
        {
            return 1;
        }
        DelayMs(2);
    }
    return 0;
}

static uint8_t max30102_read_retry(uint8_t reg_adder, uint8_t *pdata, uint8_t data_size)
{
    uint8_t retry = 3;
    while(retry--)
    {
        if(I2C_BufferRead(pdata, reg_adder, data_size))
        {
            return 1;
        }
        DelayMs(2);
    }
    return 0;
}

uint8_t max30102_i2c_write(uint8_t reg_adder, uint8_t data)
{
    return max30102_write_retry(reg_adder, data);
}

uint8_t max30102_i2c_read(uint8_t reg_adder, uint8_t *pdata, uint8_t data_size)
{
    return max30102_read_retry(reg_adder, pdata, data_size);
}

uint8_t max30102_is_ready(void)
{
    return s_max30102_ready;
}

/*
 * 让下一次 max30102_init() 跳过 once-only short-circuit、走完整的
 * 重置 + 寄存器写入流程（FIFO_WR/RD_POINTER 都会被写回 0）。
 */
void max30102_reset(void)
{
    s_max30102_inited = 0;
    s_max30102_ready  = 0;
}

uint8_t max30102_fifo_available(void)
{
    uint8_t wr_ptr = 0;
    uint8_t rd_ptr = 0;

    if(!s_max30102_ready)
    {
        return 0;
    }

    if(!max30102_i2c_read(FIFO_WR_POINTER, &wr_ptr, 1))
    {
        s_max30102_ready = 0;
        return 0;
    }

    if(!max30102_i2c_read(FIFO_RD_POINTER, &rd_ptr, 1))
    {
        s_max30102_ready = 0;
        return 0;
    }

    return (uint8_t)((wr_ptr - rd_ptr + 32U) & 0x1FU);
}

uint8_t max30102_init(void)
{
	uint8_t data;

    if(s_max30102_inited && s_max30102_ready)
    {
        return 1;
    }

    s_max30102_inited = 1;
    s_max30102_ready = 0;

	I2C_Bus_Init();   //初始化I2C接口
	DelayMs(20);

    max30102_int_gpio_init();   //中断引脚配置
    
    if(!max30102_i2c_write(MODE_CONFIGURATION, 0x40))
    {
        MPU_ERROR("MAX30102 reset write failed");
        return 0;
    }
	
	DelayMs(5);
	
    if(!max30102_i2c_write(INTERRUPT_ENABLE1, 0xE0)) return 0;
    if(!max30102_i2c_write(INTERRUPT_ENABLE2, 0x00)) return 0;  //interrupt enable: FIFO almost full flag, new FIFO Data Ready,
																						 	//                   ambient light cancellation overflow, power ready flag, 
																							//						    		internal temperature ready flag
	
    if(!max30102_i2c_write(FIFO_WR_POINTER, 0x00)) return 0;
    if(!max30102_i2c_write(FIFO_OV_COUNTER, 0x00)) return 0;
    if(!max30102_i2c_write(FIFO_RD_POINTER, 0x00)) return 0;   //clear the pointer
	
    if(!max30102_i2c_write(FIFO_CONFIGURATION, 0x4F)) return 0; //FIFO configuration: sample averaging(4),FIFO rolls on full(0), FIFO almost full value(15 empty data samples when interrupt is issued)  
	
    if(!max30102_i2c_write(MODE_CONFIGURATION, 0x03)) return 0;  //MODE configuration:SpO2 mode
	
    /* 手腕沿用原 0x6B(16384nA 量程)；勿改 0x2A，否则 raw 会从 20 万级掉到几千 */
    if(!max30102_i2c_write(SPO2_CONFIGURATION, 0x6B)) return 0;

    /* 低于 0xFF 减轻顶满，仍要有足够光强(目标 raw 约 8 万～20 万) */
    if(!max30102_i2c_write(LED1_PULSE_AMPLITUDE, 0xD0)) return 0;
    if(!max30102_i2c_write(LED2_PULSE_AMPLITUDE, 0xD8)) return 0;
	
    if(!max30102_i2c_write(TEMPERATURE_CONFIG, 0x01)) return 0;   //temp
	
    if(!max30102_i2c_read(PART_ID, &data, 1))
    {
        MPU_ERROR("MAX30102 read PART_ID failed");
        return 0;
    }
    if(data != 0x15U)
    {
        MPU_ERROR("MAX30102 PART_ID invalid: 0x%02X", data);
        return 0;
    }

    if(!max30102_i2c_read(INTERRUPT_STATUS1, &data, 1)) return 0;
    if(!max30102_i2c_read(INTERRUPT_STATUS2, &data, 1)) return 0;  //clear status
	

    s_max30102_ready = 1;
    return 1;
}


uint8_t max30102_fifo_read(float *output_data)
{
    uint8_t receive_data[6];
    uint8_t int_status1 = 0;
	uint32_t data[2];

    if(!s_max30102_ready)
    {
        return 0;
    }

    if(max30102_fifo_available() == 0U)
    {
        return 0;
    }

	if(!max30102_i2c_read(FIFO_DATA, receive_data, 6))
    {
        s_max30102_ready = 0;
        return 0;
    }

    data[0] = ((receive_data[0]<<16 | receive_data[1]<<8 | receive_data[2]) & 0x03ffff); // LED1(RED)
    data[1] = ((receive_data[3]<<16 | receive_data[4]<<8 | receive_data[5]) & 0x03ffff); // LED2(IR)
	*output_data = data[1];      // IR
	*(output_data+1) = data[0];  // RED

    if(!max30102_i2c_read(INTERRUPT_STATUS1, &int_status1, 1))
    {
        s_max30102_ready = 0;
        return 0;
    }

    return 1;
}

uint16_t max30102_getHeartRate(float *input_data, uint16_t cache_nums)
{
    float    mean = 0.0f;
    uint16_t peak_idx[16];
    uint16_t intervals[15];
    uint8_t  peak_cnt = 0U;
    uint8_t  iv_cnt   = 0U;
    uint16_t i;
    uint16_t hr_bpm;

    if(cache_nums < 32U)
    {
        return 0U;
    }

    for(i = 0U; i < cache_nums; i++)
    {
        mean += input_data[i];
    }
    mean /= (float)cache_nums;

    for(i = 0U; i < (cache_nums - 1U); i++)
    {
        if((input_data[i] <= mean) && (input_data[i + 1U] > mean))
        {
            if(peak_cnt < (uint8_t)(sizeof(peak_idx) / sizeof(peak_idx[0])))
            {
                peak_idx[peak_cnt++] = i;
            }
        }
    }

    if(peak_cnt < 2U)
    {
        return 0U;
    }

    for(i = 1U; i < peak_cnt; i++)
    {
        uint16_t d = peak_idx[i] - peak_idx[i - 1U];
        if((d >= 14U) && (d <= 100U))
        {
            if(iv_cnt < (uint8_t)(sizeof(intervals) / sizeof(intervals[0])))
            {
                intervals[iv_cnt++] = d;
            }
        }
    }

    if(iv_cnt == 0U)
    {
        return 0U;
    }

    /* 简单中值，抑制误检单间隔拉低/拉高 HR */
    for(i = 0U; i < iv_cnt; i++)
    {
        uint8_t j;
        for(j = (uint8_t)(i + 1U); j < iv_cnt; j++)
        {
            if(intervals[j] < intervals[i])
            {
                uint16_t t = intervals[i];
                intervals[i] = intervals[j];
                intervals[j] = t;
            }
        }
    }

    hr_bpm = (uint16_t)(3000U / intervals[iv_cnt / 2U]);
    if(hr_bpm < 55U && iv_cnt >= 2U)
    {
        uint16_t fast_iv = intervals[0U];

        if(fast_iv >= 12U && fast_iv < intervals[iv_cnt / 2U])
        {
            uint16_t hr_fast = (uint16_t)(3000U / fast_iv);

            if(hr_fast >= 55U && hr_fast <= 120U)
            {
                hr_bpm = hr_fast;
            }
        }
    }
    if((hr_bpm < 35U) || (hr_bpm > 180U))
    {
        return 0U;
    }
    return hr_bpm;
}

float max30102_getSpO2(float *ir_input_data,float *red_input_data,uint16_t cache_nums)
{
            float ir_sum = 0.0f, red_sum = 0.0f;
            float ir_mean, red_mean;
            float ir_ac, red_ac;
            float pi_ir, pi_red;
            float R;
            float spo2_maxim, spo2_poly, spo2;
            uint16_t i;

            if(cache_nums < 20)
            {
                return -1.0f;
            }

            for(i = 0; i < cache_nums; i++)
            {
                ir_sum += *(ir_input_data + i);
                red_sum += *(red_input_data + i);
            }

            ir_mean = ir_sum / cache_nums;
            red_mean = red_sum / cache_nums;

            if((ir_mean <= 1.0f) || (red_mean <= 1.0f))
            {
                return -1.0f;
            }

            ir_ac = spo2_segment_ac_median(ir_input_data, cache_nums);
            red_ac = spo2_segment_ac_median(red_input_data, cache_nums);
            if((ir_ac <= 1.0f) || (red_ac <= 1.0f))
            {
                return -1.0f;
            }

            pi_ir = ir_ac / ir_mean;
            pi_red = red_ac / red_mean;

            if((pi_ir < 0.0005f) || (pi_red < 0.0005f) || (pi_ir > 0.55f) || (pi_red > 0.55f))
            {
                return -1.0f;
            }

            R = pi_red / pi_ir;
            if((R < 0.20f) || (R > 1.60f))
            {
                return -1.0f;
            }
            if(R > 1.20f)
            {
                R = 1.20f;
            }

            spo2_maxim = 104.0f - 17.0f * R;
            spo2_poly = (-45.060f) * R * R + 30.354f * R + 94.845f;
            spo2 = spo2_maxim * 0.80f + spo2_poly * 0.20f;
            spo2 += WRIST_SPO2_SITE_OFFSET;
            if(spo2 > WRIST_SPO2_MAX)
            {
                spo2 = WRIST_SPO2_MAX;
            }

            if((spo2 < 70.0f) || (spo2 > WRIST_SPO2_MAX) || !(spo2 == spo2))
            {
                return -1.0f;
            }

            return spo2;
}
