#include "max30102_fir.h"

#define BLOCK_SIZE           1     
#define NUM_TAPS             29     

// 全局FIR实例
FIR_Instance_F32 S_ir, S_red;
// 状态缓存数组（大小=blockSize + numTaps - 1，与原逻辑一致）
static float firStateF32_ir[BLOCK_SIZE + NUM_TAPS - 1];
static float firStateF32_red[BLOCK_SIZE + NUM_TAPS - 1];

// 低通滤波系数（复用原系数，保证滤波效果）
const float firCoeffs32LP[NUM_TAPS] = {
  -0.001542701735,-0.002211477375,-0.003286228748, -0.00442651147,-0.004758632276,
  -0.003007677384, 0.002192312852,  0.01188309677,  0.02637642808,  0.04498152807,
    0.06596207619,   0.0867607221,   0.1044560149,   0.1163498312,   0.1205424443,
     0.1163498312,   0.1044560149,   0.0867607221,  0.06596207619,  0.04498152807,
    0.02637642808,  0.01188309677, 0.002192312852,-0.003007677384,-0.004758632276,
   -0.00442651147,-0.003286228748,-0.002211477375,-0.001542701735
};

/**
 * @brief 自定义FIR初始化函数（替代arm_fir_init_f32）
 * @param S: FIR实例指针
 * @param numTaps: 滤波抽头数
 * @param pCoeffs: 滤波系数数组
 * @param pState: 状态缓存数组
 * @param blockSize: 每次处理的样本数
 */
static void fir_init_f32(FIR_Instance_F32 *S, uint32_t numTaps, const float *pCoeffs, float *pState, uint32_t blockSize)
{
    S->numTaps = numTaps;
    S->pCoeffs = (float *)pCoeffs;
    S->pState = pState;
    S->blockSize = blockSize;

    // 初始化状态数组为0
    for(uint32_t i = 0; i < (numTaps + blockSize - 1); i++)
    {
        S->pState[i] = 0.0f;
    }
}

/**
 * @brief 自定义FIR滤波计算函数（替代arm_fir_f32）
 * @param S: FIR实例指针
 * @param pIn: 输入样本指针（单样本）
 * @param pOut: 输出样本指针
 * @param blockSize: 处理样本数（固定为1）
 */
static void fir_f32(FIR_Instance_F32 *S, const float *pIn, float *pOut, uint32_t blockSize)
{
    float *pState = S->pState;
    const float *pCoeffs = S->pCoeffs;
    float sum;
    uint32_t tap, blk;

    // 遍历每个待处理的样本（blockSize=1，仅1次循环）
    for(blk = 0; blk < blockSize; blk++)
    {
        // 将新输入样本放入状态数组头部
        pState[0] = pIn[blk];
        sum = 0.0f;

        // 卷积计算：状态数组与系数数组逐元素相乘后累加
        for(tap = 0; tap < S->numTaps; tap++)
        {
            sum += pState[tap] * pCoeffs[tap];
        }

        // 输出滤波结果
        pOut[blk] = sum;

        // 状态数组移位（丢弃最旧值，其余值后移）
        for(tap = (S->numTaps - 1); tap > 0; tap--)
        {
            pState[tap] = pState[tap - 1];
        }
    }
}

// 初始化IR和RED通道的FIR滤波器（每次调用都重置 state，便于停-启循环复位）
void max30102_fir_init(void)
{
    fir_init_f32(&S_ir, NUM_TAPS, firCoeffs32LP, firStateF32_ir, BLOCK_SIZE);
    fir_init_f32(&S_red, NUM_TAPS, firCoeffs32LP, firStateF32_red, BLOCK_SIZE);
}

// IR通道滤波接口
void ir_max30102_fir(float *input, float *output)
{
    fir_f32(&S_ir, input, output, BLOCK_SIZE);
}

// RED通道滤波接口
void red_max30102_fir(float *input, float *output)
{
    fir_f32(&S_red, input, output, BLOCK_SIZE);
}
