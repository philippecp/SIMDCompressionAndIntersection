/**
 * This code is released under the
 * Apache License Version 2.0 http://www.apache.org/licenses/.
 *
 */

#include "intersection.h"

namespace SIMDCompressionLib {

/**
 * This is often called galloping or exponential search.
 *
 * Used by frogintersectioncardinality below
 *
 * Based on binary search...
 * Find the smallest integer larger than pos such
 * that array[pos]>= min.
 * If none can be found, return array.length.
 * From code by O. Kaser.
 */
static size_t __frogadvanceUntil(const uint32_t *array, const size_t pos,
                                 const size_t length, const size_t min) {
    size_t lower = pos + 1;

    // special handling for a possibly common sequential case
    if ((lower >= length) or (array[lower] >= min)) {
        return lower;
    }

    size_t spansize = 1; // could set larger
    // bootstrap an upper limit

    while ((lower + spansize < length) and (array[lower + spansize] < min))
        spansize *= 2;
    size_t upper = (lower + spansize < length) ? lower + spansize : length - 1;

    if (array[upper] < min) {// means array has no item >= min
        return length;
    }

    // we know that the next-smallest span was too small
    lower += (spansize / 2);

    // else begin binary search
    size_t mid = 0;
    while (lower + 1 != upper) {
        mid = (lower + upper) / 2;
        if (array[mid] == min) {
            return mid;
        } else if (array[mid] < min)
            lower = mid;
        else
            upper = mid;
    }
    return upper;

}


size_t onesidedgallopingintersection(const uint32_t *smallset,
                                     const size_t smalllength, const uint32_t *largeset,
                                     const size_t largelength, uint32_t *out) {
    if (largelength < smalllength) return onesidedgallopingintersection(largeset, largelength, smallset, smalllength, out);
    if (0 == smalllength)
        return 0;
    const uint32_t *const initout(out);
    size_t k1 = 0, k2 = 0;
    while (true) {
        if (largeset[k1] < smallset[k2]) {
            k1 = __frogadvanceUntil(largeset, k1, largelength, smallset[k2]);
            if (k1 == largelength)
                break;
        }
midpoint:
        if (smallset[k2] < largeset[k1]) {
            ++k2;
            if (k2 == smalllength)
                break;
        } else {
            *out++ = smallset[k2];
            ++k2;
            if (k2 == smalllength)
                break;
            k1 = __frogadvanceUntil(largeset, k1, largelength, smallset[k2]);
            if (k1 == largelength)
                break;
            goto midpoint;
        }
    }
    return out - initout;

}



/**
 * Fast scalar scheme designed by N. Kurz.
 */
size_t scalar(const uint32_t *A, const size_t lenA,
              const uint32_t *B, const size_t lenB, uint32_t *out) {
    const uint32_t *const initout(out);
    if (lenA == 0 || lenB == 0)
        return 0;

    const uint32_t *endA = A + lenA;
    const uint32_t *endB = B + lenB;

    while (1) {
        while (*A < *B) {
SKIP_FIRST_COMPARE:
            if (++A == endA)
                return (out - initout);
        }
        while (*A > *B) {
            if (++B == endB)
                return (out - initout);
        }
        if (*A == *B) {
            *out++ = *A;
            if (++A == endA || ++B == endB)
                return (out - initout);
        } else {
            goto SKIP_FIRST_COMPARE;
        }
    }

    return (out - initout); // NOTREACHED
}





size_t match_scalar(const uint32_t *A, const size_t lenA,
                    const uint32_t *B, const size_t lenB,
                    uint32_t *out) {

    const uint32_t *initout = out;
    if (lenA == 0 || lenB == 0) return 0;

    const uint32_t *endA = A + lenA;
    const uint32_t *endB = B + lenB;

    while (1) {
        while (*A < *B) {
SKIP_FIRST_COMPARE:
            if (++A == endA) goto FINISH;
        }
        while (*A > *B) {
            if (++B == endB) goto FINISH;
        }
        if (*A == *B) {
            *out++ = *A;
            if (++A == endA || ++B == endB) goto FINISH;
        } else {
            goto SKIP_FIRST_COMPARE;
        }
    }

FINISH:
    return (out - initout);
}
#define VEC_T __m128i

/**
 * The following macros (VEC_OR, VEC_ADD_PTEST,VEC_CMP_EQUAL,VEC_SET_ALL_TO_INT,VEC_LOAD_OFFSET,
 * ASM_LEA_ADD_BYTES are only used in the v1 procedure below.
 */
#define VEC_OR(dest, other)                                             \
    __asm volatile("por %1, %0" : "+x" (dest) : "x" (other) )

// // decltype is C++ and typeof is C
#define VEC_ADD_PTEST(var, add, xmm)      {                             \
        decltype(var) _new = var + add;                                   \
        __asm volatile("ptest %2, %2\n\t"                           \
                           "cmovnz %1, %0\n\t"                          \
                           : /* writes */ "+r" (var)                    \
                           : /* reads */  "r" (_new), "x" (xmm)         \
                           : /* clobbers */ "cc");                      \
    }


#define VEC_CMP_EQUAL(dest, other)                                      \
    __asm volatile("pcmpeqd %1, %0" : "+x" (dest) : "x" (other))

#define VEC_SET_ALL_TO_INT(reg, int32)                                  \
    __asm volatile("movd %1, %0; pshufd $0, %0, %0"                 \
                       : "=x" (reg) : "g" (int32) )

#define VEC_LOAD_OFFSET(xmm, ptr, bytes)                    \
    __asm volatile("movdqu %c2(%1), %0" : "=x" (xmm) :  \
                   "r" (ptr), "i" (bytes))

#define COMPILER_LIKELY(x)     __builtin_expect((x),1)
#define COMPILER_RARELY(x)     __builtin_expect((x),0)

#define ASM_LEA_ADD_BYTES(ptr, bytes)                            \
    __asm volatile("lea %c1(%0), %0\n\t" :                       \
                   /* reads/writes %0 */  "+r" (ptr) :           \
                   /* reads */ "i" (bytes));

/**
 * Intersections scheme designed by N. Kurz that works very
 * well when intersecting an array with another where the density
 * differential is small (between 2 to 10).
 *
 * It assumes that lenRare <= lenFreq.
 *
 * Note that this is not symmetric: flipping the rare and freq pointers
 * as well as lenRare and lenFreq could lead to significant performance
 * differences.
 *
 * The matchOut pointer can safely be equal to the rare pointer.
 *
 * This function  use inline assembly.
 */
size_t v1
(const uint32_t *rare, size_t lenRare,
 const uint32_t *freq, size_t lenFreq,
 uint32_t *matchOut) {
    assert(lenRare <= lenFreq);
    const uint32_t *matchOrig = matchOut;
    if (lenFreq == 0 || lenRare == 0) return 0;

    const uint64_t kFreqSpace = 2 * 4 * (0 + 1) - 1;
    const uint64_t kRareSpace = 0;

    const uint32_t *stopFreq = &freq[lenFreq] - kFreqSpace;
    const uint32_t *stopRare = &rare[lenRare] - kRareSpace;

    VEC_T Rare;

    VEC_T F0, F1;

    if (COMPILER_RARELY((rare >= stopRare) || (freq >= stopFreq))) goto FINISH_SCALAR;

    uint64_t valRare;
    valRare = rare[0];
    VEC_SET_ALL_TO_INT(Rare, valRare);

    uint64_t maxFreq;
    maxFreq = freq[2 * 4 - 1];
    VEC_LOAD_OFFSET(F0, freq, 0 * sizeof(VEC_T)) ;
    VEC_LOAD_OFFSET(F1, freq, 1 * sizeof(VEC_T));

    if (COMPILER_RARELY(maxFreq < valRare)) goto ADVANCE_FREQ;

ADVANCE_RARE:
    do {
        *matchOut = static_cast<uint32_t>(valRare);
        valRare = rare[1]; // for next iteration
        ASM_LEA_ADD_BYTES(rare, sizeof(*rare)); // rare += 1;

        if (COMPILER_RARELY(rare >= stopRare)) {
            rare -= 1;
            goto FINISH_SCALAR;
        }

        VEC_CMP_EQUAL(F0, Rare) ;
        VEC_CMP_EQUAL(F1, Rare);

        VEC_SET_ALL_TO_INT(Rare, valRare);

        VEC_OR(F0, F1);
#ifdef __SSE4_1__
        VEC_ADD_PTEST(matchOut, 1, F0);
#else
        matchOut += static_cast<uint32_t>(_mm_movemask_epi8(F0) != 0);
#endif

        VEC_LOAD_OFFSET(F0, freq, 0 * sizeof(VEC_T)) ;
        VEC_LOAD_OFFSET(F1, freq, 1 * sizeof(VEC_T));

    } while (maxFreq >= valRare);

    uint64_t maxProbe;

ADVANCE_FREQ:
    do {
        const uint64_t kProbe = (0 + 1) * 2 * 4;
        const uint32_t *probeFreq = freq + kProbe;
        maxProbe = freq[(0 + 2) * 2 * 4 - 1];

        if (COMPILER_RARELY(probeFreq >= stopFreq)) {
            goto FINISH_SCALAR;
        }

        freq = probeFreq;

    } while (maxProbe < valRare);

    maxFreq = maxProbe;

    VEC_LOAD_OFFSET(F0, freq, 0 * sizeof(VEC_T)) ;
    VEC_LOAD_OFFSET(F1, freq, 1 * sizeof(VEC_T));

    goto ADVANCE_RARE;

    size_t count;
FINISH_SCALAR:
    count = matchOut - matchOrig;

    lenFreq = stopFreq + kFreqSpace - freq;
    lenRare = stopRare + kRareSpace - rare;

    size_t tail = match_scalar(freq, lenFreq, rare, lenRare, matchOut);

    return count + tail;
}


/**
 * This intersection function is similar to v1, but is faster when
 * the difference between lenRare and lenFreq is large, but not too large.

 * It assumes that lenRare <= lenFreq.
 *
 * Note that this is not symmetric: flipping the rare and freq pointers
 * as well as lenRare and lenFreq could lead to significant performance
 * differences.
 *
 * The matchOut pointer can safely be equal to the rare pointer.
 *
 * This function DOES NOT use inline assembly instructions. Just intrinsics.
 */
size_t v3(const uint32_t *rare, const size_t lenRare,
          const uint32_t *freq, const size_t lenFreq, uint32_t *out) {
    if (lenFreq == 0 || lenRare == 0)
        return 0;
    assert(lenRare <= lenFreq);
    const uint32_t *const initout(out);
    typedef __m128i vec;
    const uint32_t veclen = sizeof(vec) / sizeof(uint32_t);
    const size_t vecmax = veclen - 1;
    const size_t freqspace = 32 * veclen;
    const size_t rarespace = 1;

    const uint32_t *stopFreq = freq + lenFreq - freqspace;
    const uint32_t *stopRare = rare + lenRare - rarespace;
    if (freq > stopFreq) {
        return scalar(freq, lenFreq, rare, lenRare, out);
    }
    while (freq[veclen * 31 + vecmax] < *rare) {
        freq += veclen * 32;
        if (freq > stopFreq)
            goto FINISH_SCALAR;
    }
    for (; rare < stopRare; ++rare) {
        const uint32_t matchRare = *rare;//nextRare;
        const vec Match = _mm_set1_epi32(matchRare);
        while (freq[veclen * 31 + vecmax] < matchRare) { // if no match possible
            freq += veclen * 32; // advance 32 vectors
            if (freq > stopFreq)
                goto FINISH_SCALAR;
        }
        vec Q0, Q1, Q2, Q3;
        if (freq[veclen * 15 + vecmax] >= matchRare) {
            if (freq[veclen * 7 + vecmax] < matchRare) {
                Q0 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 8), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 9), Match));
                Q1 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 10), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 11), Match));

                Q2 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 12), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 13), Match));
                Q3 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 14), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 15), Match));
            } else {
                Q0 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 4), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 5), Match));
                Q1 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 6), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 7), Match));
                Q2 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 0), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 1), Match));
                Q3 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 2), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 3), Match));
            }
        } else {
            if (freq[veclen * 23 + vecmax] < matchRare) {
                Q0 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 8 + 16), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 9 + 16), Match));
                Q1 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 10 + 16), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 11 + 16), Match));

                Q2 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 12 + 16), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 13 + 16), Match));
                Q3 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 14 + 16), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 15 + 16), Match));
            } else {
                Q0 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 4 + 16), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 5 + 16), Match));
                Q1 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 6 + 16), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 7 + 16), Match));
                Q2 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 0 + 16), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 1 + 16), Match));
                Q3 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 2 + 16), Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 3 + 16), Match));
            }

        }
        const vec F0 = _mm_or_si128(_mm_or_si128(Q0, Q1), _mm_or_si128(Q2, Q3));
#ifdef __SSE4_1__
        if (_mm_testz_si128(F0, F0)) {
#else 
        if (!_mm_movemask_epi8(F0)) {
#endif 
        } else {
            *out++ = matchRare;
        }
    }

FINISH_SCALAR: return (out - initout) + scalar(freq,
                          stopFreq + freqspace - freq, rare, stopRare + rarespace - rare, out);
}


/**
 * This is the SIMD galloping function. This intersection function works well
 * when lenRare and lenFreq have vastly different values.
 *
 * It assumes that lenRare <= lenFreq.
 *
 * Note that this is not symmetric: flipping the rare and freq pointers
 * as well as lenRare and lenFreq could lead to significant performance
 * differences.
 *
 * The matchOut pointer can safely be equal to the rare pointer.
 *
 * This function DOES NOT use assembly. It only relies on intrinsics.
 */
size_t SIMDgalloping(const uint32_t *rare, const size_t lenRare,
                     const uint32_t *freq, const size_t lenFreq, uint32_t *out) {
    if (lenFreq == 0 || lenRare == 0)
        return 0;
    assert(lenRare <= lenFreq);
    const uint32_t *const initout(out);
    typedef __m128i vec;
    const uint32_t veclen = sizeof(vec) / sizeof(uint32_t);
    const size_t vecmax = veclen - 1;
    const size_t freqspace = 32 * veclen;
    const size_t rarespace = 1;

    const uint32_t *stopFreq = freq + lenFreq - freqspace;
    const uint32_t *stopRare = rare + lenRare - rarespace;
    if (freq > stopFreq) {
        return scalar(freq, lenFreq, rare, lenRare, out);
    }
    for (; rare < stopRare; ++rare) {
        const uint32_t matchRare = *rare;//nextRare;
        const vec Match = _mm_set1_epi32(matchRare);

        if (freq[veclen * 31 + vecmax] < matchRare) { // if no match possible
            uint32_t offset = 1;
            if (freq + veclen  * 32 > stopFreq) {
                freq += veclen * 32;
                goto FINISH_SCALAR;
            }
            while (freq[veclen * offset * 32 + veclen * 31 + vecmax]
                   < matchRare) { // if no match possible
                if (freq + veclen * (2 * offset) * 32 <= stopFreq) {
                    offset *= 2;
                } else if (freq + veclen * (offset + 1) * 32 <= stopFreq) {
                    offset = static_cast<uint32_t>((stopFreq - freq) / (veclen * 32));
                    //offset += 1;
                    if (freq[veclen * offset * 32 + veclen * 31 + vecmax]
                        < matchRare) {
                        freq += veclen * offset * 32;
                        goto FINISH_SCALAR;
                    } else {
                        break;
                    }
                } else {
                    freq += veclen * offset * 32;
                    goto FINISH_SCALAR;
                }
            }
            uint32_t lower = offset / 2;
            while (lower + 1 != offset) {
                const uint32_t mid = (lower + offset) / 2;
                if (freq[veclen * mid * 32 + veclen * 31 + vecmax]
                    < matchRare)
                    lower = mid;
                else
                    offset = mid;
            }
            freq += veclen * offset * 32;
        }
        vec Q0, Q1, Q2, Q3;
        if (freq[veclen * 15 + vecmax] >= matchRare) {
            if (freq[veclen * 7 + vecmax] < matchRare) {
                Q0
                    = _mm_or_si128(
                          _mm_cmpeq_epi32(
                              _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 8), Match),
                          _mm_cmpeq_epi32(
                              _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 9), Match));
                Q1 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 10),
                                         Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 11),
                                         Match));

                Q2 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 12),
                                         Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 13),
                                         Match));
                Q3 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 14),
                                         Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 15),
                                         Match));
            } else {
                Q0
                    = _mm_or_si128(
                          _mm_cmpeq_epi32(
                              _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 4), Match),
                          _mm_cmpeq_epi32(
                              _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 5), Match));
                Q1
                    = _mm_or_si128(
                          _mm_cmpeq_epi32(
                              _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 6), Match),
                          _mm_cmpeq_epi32(
                              _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 7), Match));
                Q2
                    = _mm_or_si128(
                          _mm_cmpeq_epi32(
                              _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 0), Match),
                          _mm_cmpeq_epi32(
                              _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 1), Match));
                Q3
                    = _mm_or_si128(
                          _mm_cmpeq_epi32(
                              _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 2), Match),
                          _mm_cmpeq_epi32(
                              _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 3), Match));
            }
        } else {
            if (freq[veclen * 23 + vecmax] < matchRare) {
                Q0 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 8 + 16),
                                         Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 9 + 16),
                                         Match));
                Q1 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 10 + 16),
                                         Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 11 + 16),
                                         Match));

                Q2 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 12 + 16),
                                         Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 13 + 16),
                                         Match));
                Q3 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 14 + 16),
                                         Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 15 + 16),
                                         Match));
            } else {
                Q0 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 4 + 16),
                                         Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 5 + 16),
                                         Match));
                Q1 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 6 + 16),
                                         Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 7 + 16),
                                         Match));
                Q2 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 0 + 16),
                                         Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 1 + 16),
                                         Match));
                Q3 = _mm_or_si128(
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 2 + 16),
                                         Match),
                         _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 3 + 16),
                                         Match));
            }

        }
        const vec F0 = _mm_or_si128(_mm_or_si128(Q0, Q1), _mm_or_si128(Q2, Q3));
#ifdef __SSE4_1__
        if (_mm_testz_si128(F0, F0)) {
#else 
        if (!_mm_movemask_epi8(F0)) {
#endif 
        } else {
            *out++ = matchRare;
        }
    }

FINISH_SCALAR: return (out - initout) + scalar(freq,
                          stopFreq + freqspace - freq, rare, stopRare + rarespace - rare, out);
}

/**
 * Our main heuristic.
 *
 * The out pointer can be set1 if length1<=length2,
 * or else it can be set2 if length2>length1.
 */
size_t SIMDintersection(const uint32_t *set1,
                        const size_t length1, const uint32_t *set2, const size_t length2, uint32_t *out) {
    if ((length1 == 0) or (length2 == 0)) return 0;


    if ((1000 * length1 <= length2) or (1000 * length2 <= length1)) {
        if (length1 <= length2)
            return SIMDgalloping(set1, length1, set2, length2, out);
        else
            return SIMDgalloping(set2, length2, set1, length1, out);
    }

    if ((50 * length1 <= length2) or (50 * length2 <= length1)) {
        if (length1 <= length2)
            return v3(set1, length1, set2, length2, out);
        else
            return v3(set2, length2, set1, length1, out);
    }

    if (length1 <= length2)
        return v1(set1, length1, set2, length2, out);
    else
        return v1(set2, length2, set1, length1, out);
}
/**
 * More or less from
 * http://highlyscalable.wordpress.com/2012/06/05/fast-intersection-sorted-lists-sse/
 */
const static __m128i shuffle_mask[16] = {
        _mm_set_epi8(15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0),
        _mm_set_epi8(15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0),
        _mm_set_epi8(15,14,13,12,11,10,9,8,7,6,5,4,7,6,5,4),
        _mm_set_epi8(15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0),
        _mm_set_epi8(15,14,13,12,11,10,9,8,7,6,5,4,11,10,9,8),
        _mm_set_epi8(15,14,13,12,11,10,9,8,11,10,9,8,3,2,1,0),
        _mm_set_epi8(15,14,13,12,11,10,9,8,11,10,9,8,7,6,5,4),
        _mm_set_epi8(15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0),
        _mm_set_epi8(15,14,13,12,11,10,9,8,7,6,5,4,15,14,13,12),
        _mm_set_epi8(15,14,13,12,11,10,9,8,15,14,13,12,3,2,1,0),
        _mm_set_epi8(15,14,13,12,11,10,9,8,15,14,13,12,7,6,5,4),
        _mm_set_epi8(15,14,13,12,15,14,13,12,7,6,5,4,3,2,1,0),
        _mm_set_epi8(15,14,13,12,11,10,9,8,15,14,13,12,11,10,9,8),
        _mm_set_epi8(15,14,13,12,15,14,13,12,11,10,9,8,3,2,1,0),
        _mm_set_epi8(15,14,13,12,15,14,13,12,11,10,9,8,7,6,5,4),
        _mm_set_epi8(15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0),
        };
// precomputed dictionary


/**
 * Taken almost verbatim from http://highlyscalable.wordpress.com/2012/06/05/fast-intersection-sorted-lists-sse/
 *
 * It is not safe for out to be either A or B.
 */
size_t highlyscalable_intersect_SIMD(const uint32_t *A, const size_t s_a,
        const uint32_t *B, const size_t s_b, uint32_t * out) {
    assert(out != A);
    assert(out != B);
    const uint32_t * const initout (out);
    size_t i_a = 0, i_b = 0;

    // trim lengths to be a multiple of 4
    size_t st_a = (s_a / 4) * 4;
    size_t st_b = (s_b / 4) * 4;

    while (i_a < st_a && i_b < st_b) {
        //[ load segments of four 32-bit elements
        __m128i v_a = _mm_loadu_si128((__m128i *) &A[i_a]);
        __m128i v_b = _mm_loadu_si128((__m128i *) &B[i_b]);
        //]

        //[ move pointers
        const uint32_t a_max = A[i_a + 3];
        const uint32_t b_max = B[i_b + 3];
        i_a += (a_max <= b_max) * 4;
        i_b += (a_max >= b_max) * 4;
        //]

        //[ compute mask of common elements
        const uint32_t cyclic_shift = _MM_SHUFFLE(0, 3, 2, 1);
        __m128i cmp_mask1 = _mm_cmpeq_epi32(v_a, v_b); // pairwise comparison
        v_b = _mm_shuffle_epi32(v_b, cyclic_shift); // shuffling
        __m128i cmp_mask2 = _mm_cmpeq_epi32(v_a, v_b); // again...
        v_b = _mm_shuffle_epi32(v_b, cyclic_shift);
        __m128i cmp_mask3 = _mm_cmpeq_epi32(v_a, v_b); // and again...
        v_b = _mm_shuffle_epi32(v_b, cyclic_shift);
        __m128i cmp_mask4 = _mm_cmpeq_epi32(v_a, v_b); // and again.
        __m128i cmp_mask = _mm_or_si128(_mm_or_si128(cmp_mask1, cmp_mask2),
                _mm_or_si128(cmp_mask3, cmp_mask4)); // OR-ing of comparison masks
        // convert the 128-bit mask to the 4-bit mask
        const int mask = _mm_movemask_ps((__m128 ) cmp_mask);
        //]

        //[ copy out common elements
        const __m128i p = _mm_shuffle_epi8(v_a, shuffle_mask[mask]);
        _mm_storeu_si128((__m128i*)out, p);
        out += _mm_popcnt_u32(mask); // a number of elements is a weight of the mask
        //]
    }

    // intersect the tail using scalar intersection
    while (i_a < s_a && i_b < s_b) {
        if (A[i_a] < B[i_b]) {
            i_a++;
        } else if (B[i_b] < A[i_a]) {
            i_b++;
        } else {
            *out++ = B[i_b]; ;
            i_a++;
            i_b++;
        }
    }

    return out - initout;
}



inline std::map<std::string, intersectionfunction> initializeintersectionfactory() {
    std::map<std::string, intersectionfunction> schemes;
    schemes[ "simd" ] = SIMDintersection;
    schemes[ "galloping" ] = onesidedgallopingintersection;
    schemes[ "scalar" ] = scalar;
    schemes[ "v1" ] = v1;
    schemes["v3"] = v3;
    schemes["simdgalloping"] = SIMDgalloping;
    schemes["highlyscalable_intersect_SIMD"] = highlyscalable_intersect_SIMD;
    return schemes;
}

std::map<std::string, intersectionfunction> IntersectionFactory::intersection_schemes = initializeintersectionfactory();


} // namespace SIMDCompressionLib
