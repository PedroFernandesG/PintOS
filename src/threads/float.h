#ifndef THREADS_FLOAT_H
#define THREADS_FLOAT_H

typedef int float_type;

#define FLOAT_SHIFT_AMOUNT 15

// int -> float
#define FLOAT_CONST(A) ((float_type)(A << FLOAT_SHIFT_AMOUNT)) 

// float + float
#define FLOAT_ADD(A,B) (A + B)

// float + int
#define FLOAT_ADD_MIX(A,B) (A + (B << FLOAT_SHIFT_AMOUNT))

// float - float
#define FLOAT_SUB(A,B) (A - B)

// float - int
#define FLOAT_SUB_MIX(A,B) (A - (B << FLOAT_SHIFT_AMOUNT))

// float * int
#define FLOAT_MULT_MIX(A,B) (A * B)

// float / int
#define FLOAT_DIV_MIX(A,B) (A / B)

// float * float
#define FLOAT_MULT(A,B) ((float_type)(((int64_t) A) * B >> FLOAT_SHIFT_AMOUNT))

// float / float
#define FLOAT_DIV(A,B) ((float_type)((((int64_t) A) << FLOAT_SHIFT_AMOUNT) / B))

// Pega a parte inteira do numero (funcao floor)
#define FLOAT_INT_PART(A) (A >> FLOAT_SHIFT_AMOUNT)

// Arredondamento considerando decimal (Ex: 1.5 = 2)
#define FLOAT_ROUND(A) (A >= 0 ? ((A + (1 << (FLOAT_SHIFT_AMOUNT - 1))) >> FLOAT_SHIFT_AMOUNT) \
        : ((A - (1 << (FLOAT_SHIFT_AMOUNT - 1))) >> FLOAT_SHIFT_AMOUNT))

#endif