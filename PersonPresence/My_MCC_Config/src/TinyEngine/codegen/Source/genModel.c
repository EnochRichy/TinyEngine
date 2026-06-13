/* Automatically generated source file */
#include <float.h>
#include <tinyengine_function.h>
#include <tinyengine_function_fp.h>

#include "genNN.h"
#include "genModel.h"
#include "genInclude.h"

/* Variables used by all ops */
ADD_params add_params;
int i;
int8_t *int8ptr,*int8ptr2;
int32_t *int32ptr;
float *fptr,*fptr2,*fptr3;

signed char* getInput() {
    return &buffer0[16384];
}
signed char* getOutput() {
    return NNoutput;
}
void end2endinference(q7_t* img){
    invoke(NULL);
}
void invoke(float* labels){
/* layer 0:CONV_2D */
convolve_s8_kernel3_inputch3_stride2_pad1_fpreq(&buffer0[16384],64,64,3,(const q7_t*) weight0,bias0,scales0,-128,1,-128,127,&buffer0[0],32,32,16,sbuf,kbuf,-1);
/* layer 1:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],32,32,16,(const q7_t*) CHWweight1,offsetBias1,offsetRBias1,scales1,-128,128,-128,127,&buffer0[0],32,32,16,sbuf,-128);
/* layer 2:CONV_2D */
convolve_1x1_s8_ch16_fpreq(&buffer0[0],32,32,16,(const q7_t*) weight2,bias2,scales2,0,128,-128,127,&buffer0[49152],32,32,8,sbuf);
/* layer 3:CONV_2D */
convolve_1x1_s8_ch8_fpreq(&buffer0[49152],32,32,8,(const q7_t*) weight3,bias3,scales3,-128,0,-128,127,&buffer0[0],32,32,48,sbuf);
/* layer 4:DEPTHWISE_CONV_2D */
depthwise_kernel5x5_stride2_inplace_CHW_fpreq(&buffer0[0],32,32,48,(const q7_t*) CHWweight4,offsetBias4,offsetRBias4,scales4,-128,128,-128,127,&buffer0[0],16,16,48,sbuf,-128);
/* layer 5:CONV_2D */
convolve_1x1_s8_ch48_fpreq(&buffer0[0],16,16,48,(const q7_t*) weight5,bias5,scales5,-22,128,-128,127,&buffer0[16384],16,16,16,sbuf);
/* layer 6:CONV_2D */
convolve_1x1_s8_ch16_fpreq(&buffer0[16384],16,16,16,(const q7_t*) weight6,bias6,scales6,-128,22,-128,127,&buffer0[0],16,16,64,sbuf);
/* layer 7:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],16,16,64,(const q7_t*) CHWweight7,offsetBias7,offsetRBias7,scales7,-128,128,-128,127,&buffer0[0],16,16,64,sbuf,-128);
/* layer 8:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],16,16,64,(const q7_t*) weight8,bias8,scales8,6,128,-128,127,&buffer0[20480],16,16,16,sbuf);
/* layer 9:ADD */
add_fpreq(4096, &buffer0[20480],0.07058437913656235,6,&buffer0[16384],0.08491315692663193,-22,0.09407555311918259,-7,&buffer0[24576]);
/* layer 10:CONV_2D */
convolve_1x1_s8_ch16_fpreq(&buffer0[24576],16,16,16,(const q7_t*) weight9,bias9,scales9,-128,7,-128,127,&buffer0[0],16,16,80,sbuf);
/* layer 11:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride2_inplace_CHW_fpreq(&buffer0[0],16,16,80,(const q7_t*) CHWweight10,offsetBias10,offsetRBias10,scales10,-128,128,-128,127,&buffer0[0],8,8,80,sbuf,-128);
/* layer 12:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],8,8,80,(const q7_t*) weight11,bias11,scales11,5,128,-128,127,&buffer0[6144],8,8,24,sbuf);
/* layer 13:CONV_2D */
convolve_1x1_s8_ch24_fpreq(&buffer0[6144],8,8,24,(const q7_t*) weight12,bias12,scales12,-128,-5,-128,127,&buffer0[0],8,8,96,sbuf);
/* layer 14:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],8,8,96,(const q7_t*) CHWweight13,offsetBias13,offsetRBias13,scales13,-128,128,-128,127,&buffer0[0],8,8,96,sbuf,-128);
/* layer 15:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],8,8,96,(const q7_t*) weight14,bias14,scales14,2,128,-128,127,&buffer0[7680],8,8,24,sbuf);
/* layer 16:ADD */
add_fpreq(1536, &buffer0[7680],0.059268780052661896,2,&buffer0[6144],0.0610022135078907,5,0.06897635757923126,1,&buffer0[9216]);
/* layer 17:CONV_2D */
convolve_1x1_s8_ch24_fpreq(&buffer0[9216],8,8,24,(const q7_t*) weight15,bias15,scales15,-128,-1,-128,127,&buffer0[0],8,8,96,sbuf);
/* layer 18:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],8,8,96,(const q7_t*) CHWweight16,offsetBias16,offsetRBias16,scales16,-128,128,-128,127,&buffer0[0],8,8,96,sbuf,-128);
/* layer 19:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],8,8,96,(const q7_t*) weight17,bias17,scales17,-6,128,-128,127,&buffer0[6144],8,8,24,sbuf);
/* layer 20:ADD */
add_fpreq(1536, &buffer0[6144],0.028355341404676437,-6,&buffer0[9216],0.06897635757923126,1,0.07479318976402283,7,&buffer0[7680]);
/* layer 21:CONV_2D */
convolve_1x1_s8_ch24_fpreq(&buffer0[7680],8,8,24,(const q7_t*) weight18,bias18,scales18,-128,-7,-128,127,&buffer0[0],8,8,120,sbuf);
/* layer 22:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride2_inplace_CHW_fpreq(&buffer0[0],8,8,120,(const q7_t*) CHWweight19,offsetBias19,offsetRBias19,scales19,-128,128,-128,127,&buffer0[0],4,4,120,sbuf,-128);
/* layer 23:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],4,4,120,(const q7_t*) weight20,bias20,scales20,-18,128,-128,127,&buffer0[2560],4,4,40,sbuf);
/* layer 24:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[2560],4,4,40,(const q7_t*) weight21,bias21,scales21,-128,18,-128,127,&buffer0[0],4,4,160,sbuf);
/* layer 25:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],4,4,160,(const q7_t*) CHWweight22,offsetBias22,offsetRBias22,scales22,-128,128,-128,127,&buffer0[0],4,4,160,sbuf,-128);
/* layer 26:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],4,4,160,(const q7_t*) weight23,bias23,scales23,-8,128,-128,127,&buffer0[3200],4,4,40,sbuf);
/* layer 27:ADD */
add_fpreq(640, &buffer0[3200],0.04279512166976929,-8,&buffer0[2560],0.05035131052136421,-18,0.05565929785370827,-9,&buffer0[1920]);
/* layer 28:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[1920],4,4,40,(const q7_t*) weight24,bias24,scales24,-128,9,-128,127,&buffer0[0],4,4,120,sbuf);
/* layer 29:DEPTHWISE_CONV_2D */
depthwise_kernel7x7_stride1_inplace_CHW_fpreq(&buffer0[0],4,4,120,(const q7_t*) CHWweight25,offsetBias25,offsetRBias25,scales25,-128,128,-128,127,&buffer0[0],4,4,120,sbuf,-128);
/* layer 30:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],4,4,120,(const q7_t*) weight26,bias26,scales26,25,128,-128,127,&buffer0[2560],4,4,40,sbuf);
/* layer 31:ADD */
add_fpreq(640, &buffer0[2560],0.01802844926714897,25,&buffer0[1920],0.05565929785370827,-9,0.05594513937830925,-11,&buffer0[3200]);
/* layer 32:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[3200],4,4,40,(const q7_t*) weight27,bias27,scales27,-128,11,-128,127,&buffer0[0],4,4,120,sbuf);
/* layer 33:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],4,4,120,(const q7_t*) CHWweight28,offsetBias28,offsetRBias28,scales28,-128,128,-128,127,&buffer0[0],4,4,120,sbuf,-128);
/* layer 34:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],4,4,120,(const q7_t*) weight29,bias29,scales29,-2,128,-128,127,&buffer0[3072],4,4,48,sbuf);
/* layer 35:CONV_2D */
convolve_1x1_s8_ch48_fpreq(&buffer0[3072],4,4,48,(const q7_t*) weight30,bias30,scales30,-128,2,-128,127,&buffer0[0],4,4,192,sbuf);
/* layer 36:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],4,4,192,(const q7_t*) CHWweight31,offsetBias31,offsetRBias31,scales31,-128,128,-128,127,&buffer0[0],4,4,192,sbuf,-128);
/* layer 37:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],4,4,192,(const q7_t*) weight32,bias32,scales32,-11,128,-128,127,&buffer0[3840],4,4,48,sbuf);
/* layer 38:ADD */
add_fpreq(768, &buffer0[3840],0.04790326580405235,-11,&buffer0[3072],0.03588924929499626,-2,0.04805959761142731,-1,&buffer0[4608]);
/* layer 39:CONV_2D */
convolve_1x1_s8_ch48_fpreq(&buffer0[4608],4,4,48,(const q7_t*) weight33,bias33,scales33,-128,1,-128,127,&buffer0[0],4,4,240,sbuf);
/* layer 40:DEPTHWISE_CONV_2D */
depthwise_kernel7x7_stride2_inplace_CHW_fpreq(&buffer0[0],4,4,240,(const q7_t*) CHWweight34,offsetBias34,offsetRBias34,scales34,-128,128,-128,127,&buffer0[0],2,2,240,sbuf,-128);
/* layer 41:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],2,2,240,(const q7_t*) weight35,bias35,scales35,-15,128,-128,127,&buffer0[1920],2,2,96,sbuf);
/* layer 42:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[1920],2,2,96,(const q7_t*) weight36,bias36,scales36,-128,15,-128,127,&buffer0[0],2,2,480,sbuf);
/* layer 43:DEPTHWISE_CONV_2D */
depthwise_kernel5x5_stride1_inplace_CHW_fpreq(&buffer0[0],2,2,480,(const q7_t*) CHWweight37,offsetBias37,offsetRBias37,scales37,-128,128,-128,127,&buffer0[0],2,2,480,sbuf,-128);
/* layer 44:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],2,2,480,(const q7_t*) weight38,bias38,scales38,4,128,-128,127,&buffer0[2304],2,2,96,sbuf);
/* layer 45:ADD */
add_fpreq(384, &buffer0[2304],0.0305772852152586,4,&buffer0[1920],0.03393186256289482,-15,0.032982613891363144,0,&buffer0[1536]);
/* layer 46:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[1536],2,2,96,(const q7_t*) weight39,bias39,scales39,-128,0,-128,127,&buffer0[0],2,2,384,sbuf);
/* layer 47:DEPTHWISE_CONV_2D */
depthwise_kernel7x7_stride1_inplace_CHW_fpreq(&buffer0[0],2,2,384,(const q7_t*) CHWweight40,offsetBias40,offsetRBias40,scales40,-128,128,-128,127,&buffer0[0],2,2,384,sbuf,-128);
/* layer 48:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],2,2,384,(const q7_t*) weight41,bias41,scales41,12,128,-128,127,&buffer0[1536],2,2,160,sbuf);
/* layer 49:AVERAGE_POOL_2D */
avg_pooling(&buffer0[1536],2,2,160,2,2,1,1,-128,127,&buffer0[0]);
/* layer 50:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],1,1,160,(const q7_t*) weight42,bias42,scales42,0,-12,-128,127,&buffer0[160],1,1,2,sbuf);
}
void invoke_inf(){
/* layer 0:CONV_2D */
convolve_s8_kernel3_inputch3_stride2_pad1_fpreq(&buffer0[16384],64,64,3,(const q7_t*) weight0,bias0,scales0,-128,1,-128,127,&buffer0[0],32,32,16,sbuf,kbuf,-1);
/* layer 1:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],32,32,16,(const q7_t*) CHWweight1,offsetBias1,offsetRBias1,scales1,-128,128,-128,127,&buffer0[0],32,32,16,sbuf,-128);
/* layer 2:CONV_2D */
convolve_1x1_s8_ch16_fpreq(&buffer0[0],32,32,16,(const q7_t*) weight2,bias2,scales2,0,128,-128,127,&buffer0[49152],32,32,8,sbuf);
/* layer 3:CONV_2D */
convolve_1x1_s8_ch8_fpreq(&buffer0[49152],32,32,8,(const q7_t*) weight3,bias3,scales3,-128,0,-128,127,&buffer0[0],32,32,48,sbuf);
/* layer 4:DEPTHWISE_CONV_2D */
depthwise_kernel5x5_stride2_inplace_CHW_fpreq(&buffer0[0],32,32,48,(const q7_t*) CHWweight4,offsetBias4,offsetRBias4,scales4,-128,128,-128,127,&buffer0[0],16,16,48,sbuf,-128);
/* layer 5:CONV_2D */
convolve_1x1_s8_ch48_fpreq(&buffer0[0],16,16,48,(const q7_t*) weight5,bias5,scales5,-22,128,-128,127,&buffer0[16384],16,16,16,sbuf);
/* layer 6:CONV_2D */
convolve_1x1_s8_ch16_fpreq(&buffer0[16384],16,16,16,(const q7_t*) weight6,bias6,scales6,-128,22,-128,127,&buffer0[0],16,16,64,sbuf);
/* layer 7:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],16,16,64,(const q7_t*) CHWweight7,offsetBias7,offsetRBias7,scales7,-128,128,-128,127,&buffer0[0],16,16,64,sbuf,-128);
/* layer 8:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],16,16,64,(const q7_t*) weight8,bias8,scales8,6,128,-128,127,&buffer0[20480],16,16,16,sbuf);
/* layer 9:ADD */
add_fpreq(4096, &buffer0[20480],0.07058437913656235,6,&buffer0[16384],0.08491315692663193,-22,0.09407555311918259,-7,&buffer0[24576]);
/* layer 10:CONV_2D */
convolve_1x1_s8_ch16_fpreq(&buffer0[24576],16,16,16,(const q7_t*) weight9,bias9,scales9,-128,7,-128,127,&buffer0[0],16,16,80,sbuf);
/* layer 11:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride2_inplace_CHW_fpreq(&buffer0[0],16,16,80,(const q7_t*) CHWweight10,offsetBias10,offsetRBias10,scales10,-128,128,-128,127,&buffer0[0],8,8,80,sbuf,-128);
/* layer 12:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],8,8,80,(const q7_t*) weight11,bias11,scales11,5,128,-128,127,&buffer0[6144],8,8,24,sbuf);
/* layer 13:CONV_2D */
convolve_1x1_s8_ch24_fpreq(&buffer0[6144],8,8,24,(const q7_t*) weight12,bias12,scales12,-128,-5,-128,127,&buffer0[0],8,8,96,sbuf);
/* layer 14:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],8,8,96,(const q7_t*) CHWweight13,offsetBias13,offsetRBias13,scales13,-128,128,-128,127,&buffer0[0],8,8,96,sbuf,-128);
/* layer 15:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],8,8,96,(const q7_t*) weight14,bias14,scales14,2,128,-128,127,&buffer0[7680],8,8,24,sbuf);
/* layer 16:ADD */
add_fpreq(1536, &buffer0[7680],0.059268780052661896,2,&buffer0[6144],0.0610022135078907,5,0.06897635757923126,1,&buffer0[9216]);
/* layer 17:CONV_2D */
convolve_1x1_s8_ch24_fpreq(&buffer0[9216],8,8,24,(const q7_t*) weight15,bias15,scales15,-128,-1,-128,127,&buffer0[0],8,8,96,sbuf);
/* layer 18:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],8,8,96,(const q7_t*) CHWweight16,offsetBias16,offsetRBias16,scales16,-128,128,-128,127,&buffer0[0],8,8,96,sbuf,-128);
/* layer 19:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],8,8,96,(const q7_t*) weight17,bias17,scales17,-6,128,-128,127,&buffer0[6144],8,8,24,sbuf);
/* layer 20:ADD */
add_fpreq(1536, &buffer0[6144],0.028355341404676437,-6,&buffer0[9216],0.06897635757923126,1,0.07479318976402283,7,&buffer0[7680]);
/* layer 21:CONV_2D */
convolve_1x1_s8_ch24_fpreq(&buffer0[7680],8,8,24,(const q7_t*) weight18,bias18,scales18,-128,-7,-128,127,&buffer0[0],8,8,120,sbuf);
/* layer 22:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride2_inplace_CHW_fpreq(&buffer0[0],8,8,120,(const q7_t*) CHWweight19,offsetBias19,offsetRBias19,scales19,-128,128,-128,127,&buffer0[0],4,4,120,sbuf,-128);
/* layer 23:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],4,4,120,(const q7_t*) weight20,bias20,scales20,-18,128,-128,127,&buffer0[2560],4,4,40,sbuf);
/* layer 24:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[2560],4,4,40,(const q7_t*) weight21,bias21,scales21,-128,18,-128,127,&buffer0[0],4,4,160,sbuf);
/* layer 25:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],4,4,160,(const q7_t*) CHWweight22,offsetBias22,offsetRBias22,scales22,-128,128,-128,127,&buffer0[0],4,4,160,sbuf,-128);
/* layer 26:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],4,4,160,(const q7_t*) weight23,bias23,scales23,-8,128,-128,127,&buffer0[3200],4,4,40,sbuf);
/* layer 27:ADD */
add_fpreq(640, &buffer0[3200],0.04279512166976929,-8,&buffer0[2560],0.05035131052136421,-18,0.05565929785370827,-9,&buffer0[1920]);
/* layer 28:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[1920],4,4,40,(const q7_t*) weight24,bias24,scales24,-128,9,-128,127,&buffer0[0],4,4,120,sbuf);
/* layer 29:DEPTHWISE_CONV_2D */
depthwise_kernel7x7_stride1_inplace_CHW_fpreq(&buffer0[0],4,4,120,(const q7_t*) CHWweight25,offsetBias25,offsetRBias25,scales25,-128,128,-128,127,&buffer0[0],4,4,120,sbuf,-128);
/* layer 30:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],4,4,120,(const q7_t*) weight26,bias26,scales26,25,128,-128,127,&buffer0[2560],4,4,40,sbuf);
/* layer 31:ADD */
add_fpreq(640, &buffer0[2560],0.01802844926714897,25,&buffer0[1920],0.05565929785370827,-9,0.05594513937830925,-11,&buffer0[3200]);
/* layer 32:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[3200],4,4,40,(const q7_t*) weight27,bias27,scales27,-128,11,-128,127,&buffer0[0],4,4,120,sbuf);
/* layer 33:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],4,4,120,(const q7_t*) CHWweight28,offsetBias28,offsetRBias28,scales28,-128,128,-128,127,&buffer0[0],4,4,120,sbuf,-128);
/* layer 34:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],4,4,120,(const q7_t*) weight29,bias29,scales29,-2,128,-128,127,&buffer0[3072],4,4,48,sbuf);
/* layer 35:CONV_2D */
convolve_1x1_s8_ch48_fpreq(&buffer0[3072],4,4,48,(const q7_t*) weight30,bias30,scales30,-128,2,-128,127,&buffer0[0],4,4,192,sbuf);
/* layer 36:DEPTHWISE_CONV_2D */
depthwise_kernel3x3_stride1_inplace_CHW_fpreq(&buffer0[0],4,4,192,(const q7_t*) CHWweight31,offsetBias31,offsetRBias31,scales31,-128,128,-128,127,&buffer0[0],4,4,192,sbuf,-128);
/* layer 37:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],4,4,192,(const q7_t*) weight32,bias32,scales32,-11,128,-128,127,&buffer0[3840],4,4,48,sbuf);
/* layer 38:ADD */
add_fpreq(768, &buffer0[3840],0.04790326580405235,-11,&buffer0[3072],0.03588924929499626,-2,0.04805959761142731,-1,&buffer0[4608]);
/* layer 39:CONV_2D */
convolve_1x1_s8_ch48_fpreq(&buffer0[4608],4,4,48,(const q7_t*) weight33,bias33,scales33,-128,1,-128,127,&buffer0[0],4,4,240,sbuf);
/* layer 40:DEPTHWISE_CONV_2D */
depthwise_kernel7x7_stride2_inplace_CHW_fpreq(&buffer0[0],4,4,240,(const q7_t*) CHWweight34,offsetBias34,offsetRBias34,scales34,-128,128,-128,127,&buffer0[0],2,2,240,sbuf,-128);
/* layer 41:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],2,2,240,(const q7_t*) weight35,bias35,scales35,-15,128,-128,127,&buffer0[1920],2,2,96,sbuf);
/* layer 42:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[1920],2,2,96,(const q7_t*) weight36,bias36,scales36,-128,15,-128,127,&buffer0[0],2,2,480,sbuf);
/* layer 43:DEPTHWISE_CONV_2D */
depthwise_kernel5x5_stride1_inplace_CHW_fpreq(&buffer0[0],2,2,480,(const q7_t*) CHWweight37,offsetBias37,offsetRBias37,scales37,-128,128,-128,127,&buffer0[0],2,2,480,sbuf,-128);
/* layer 44:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],2,2,480,(const q7_t*) weight38,bias38,scales38,4,128,-128,127,&buffer0[2304],2,2,96,sbuf);
/* layer 45:ADD */
add_fpreq(384, &buffer0[2304],0.0305772852152586,4,&buffer0[1920],0.03393186256289482,-15,0.032982613891363144,0,&buffer0[1536]);
/* layer 46:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[1536],2,2,96,(const q7_t*) weight39,bias39,scales39,-128,0,-128,127,&buffer0[0],2,2,384,sbuf);
/* layer 47:DEPTHWISE_CONV_2D */
depthwise_kernel7x7_stride1_inplace_CHW_fpreq(&buffer0[0],2,2,384,(const q7_t*) CHWweight40,offsetBias40,offsetRBias40,scales40,-128,128,-128,127,&buffer0[0],2,2,384,sbuf,-128);
/* layer 48:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],2,2,384,(const q7_t*) weight41,bias41,scales41,12,128,-128,127,&buffer0[1536],2,2,160,sbuf);
/* layer 49:AVERAGE_POOL_2D */
avg_pooling(&buffer0[1536],2,2,160,2,2,1,1,-128,127,&buffer0[0]);
/* layer 50:CONV_2D */
convolve_1x1_s8_fpreq(&buffer0[0],1,1,160,(const q7_t*) weight42,bias42,scales42,0,-12,-128,127,&buffer0[160],1,1,2,sbuf);
}
