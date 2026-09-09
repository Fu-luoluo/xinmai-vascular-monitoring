#ifndef __MAX30102_FIR_H
#define __MAX30102_FIR_H

#include <stdint.h>

// 自定义FIR滤波实例结构体（替代ARM DSP的arm_fir_instance_f32）
typedef struct
{
    uint32_t numTaps;      // 滤波抽头数
    float *pCoeffs;        // 滤波系数数组指针
    float *pState;         // 状态缓存数组指针
    uint32_t blockSize;    // 每次处理的样本数（保持原1）
} FIR_Instance_F32;

#define BLOCK_SIZE           1     // 每次处理1个样本
#define NUM_TAPS             29     // 滤波抽头数（与原系数数量一致）

// 全局FIR实例（对应IR和RED通道）
extern FIR_Instance_F32 S_ir, S_red;
// 滤波系数（复用原系数，保持滤波特性）
extern const float firCoeffs32LP[NUM_TAPS];

void max30102_fir_init(void);
void ir_max30102_fir(float *input, float *output);
void red_max30102_fir(float *input, float *output);

#endif /* __MAX30102_FIR_H */
