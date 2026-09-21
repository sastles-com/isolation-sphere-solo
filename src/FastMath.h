#ifndef __FAST_MATH_H__
#define __FAST_MATH_H__

#include <cmath>

// LED座標変換(sphereToUV)用の高速計算関数群。
// 以前は double 演算(frexp/ldexp + 4次収束ループ + double literal)で、
// ESP32 では double がソフトエミュレーションのため非常に遅かった
// (実測: マッピング 27ms/フレーム)。数式・出力規約はそのままに、
// ハードウェア単精度 sqrtf と float 演算へ置き換えて高速化する。
// (px/py は最終的に整数量子化されるため、描画結果は実質同一)

static inline float _sqrt(float a){
    if(a < 0.0f){
        return 0.0f;
    }
    return sqrtf(a);  // ESP32-S3 FPU のハードウェア単精度平方根
}


static inline float _atan2(float _y, float _x){
    float x = fabsf(_x);
    float y = fabsf(_y);
    float z;
    bool c;


    if(y < x){
      z = y/x;
      c = true;
    }else{
      z = x/y;
      c = false;
    }
    float a = 8.0928f*z*z*z*z-19.657f*z*z*z-0.9258f*z*z+57.511f*z-0.0083f;
    if(_x == 0.0f){
        if(_y > 0.0f)    a = 90.0f;
        else             a = -90.0f;
    }
    if(c){    // a<1
        if(_x > 0.0f){
            if(_y < 0.0f)  a *= -1.0f;
        }
        if(_x < 0.0f){
            if(_y > 0.0f)  a = 180.0f - a;
            if(_y < 0.0f)  a = a - 180.0f;
        }
    }
    if(!c){   // a>1
        if(_x > 0.0f){
            if(_y > 0.0f)  a = 90.0f - a;
            if(_y < 0.0f)  a = a - 90.0f;
        }
        if(_x < 0.0f){
            if(_y > 0.0f)  a = a + 90.0f;
            if(_y < 0.0f)  a = -a - 90.0f;
        }
    }
    return a/180.0f;
}

#endif /* __FAST_MATH_H__ */
