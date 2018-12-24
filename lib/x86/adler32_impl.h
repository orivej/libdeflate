/*
 * x86/adler32_impl.h - x86 implementations of Adler-32 checksum algorithm
 *
 * Copyright 2016 Eric Biggers
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "cpu_features.h"

/*
 * The following macros horizontally sum the s1 counters and add them to s1, and
 * likewise for s2.  They do a series of reduction steps where the vector
 * length, and hence the number of 32-bit counters, is halved each time.
 *
 * For efficiency the reductions of the s1 and s2 counters are interleaved,
 * since they don't depend on each other.  Note also that every other s1 counter
 * is 0 due to the 'psadbw' instruction (_mm_sad_epu8) summing 8 bytes at a time
 * rather than 4, hence one of the s1 reduction steps can be skipped.
 */

#define ADLER32_FINISH_VEC_CHUNK_128(s1, s2, v_s1, v_s2)		   \
{									   \
	__v4si s1_fin = (v_s1), s2_fin = (v_s2);			   \
									   \
	/* 128 => 32 bits */						   \
	s2_fin += (__v4si)_mm_shuffle_epi32((__m128i)s2_fin, 0x31);	   \
	s1_fin += (__v4si)_mm_shuffle_epi32((__m128i)s1_fin, 0x02);	   \
	s2_fin += (__v4si)_mm_shuffle_epi32((__m128i)s2_fin, 0x02);	   \
									   \
	*(s1) += (u32)_mm_cvtsi128_si32((__m128i)s1_fin);		   \
	*(s2) += (u32)_mm_cvtsi128_si32((__m128i)s2_fin);		   \
}

#define ADLER32_FINISH_VEC_CHUNK_256(s1, s2, v_s1, v_s2)		    \
{									    \
	__v4si s1_128bit, s2_128bit;					    \
									    \
	/* 256 => 128 bits */						    \
	s1_128bit = (__v4si)_mm256_extracti128_si256((__m256i)(v_s1), 0) +  \
		    (__v4si)_mm256_extracti128_si256((__m256i)(v_s1), 1);   \
	s2_128bit = (__v4si)_mm256_extracti128_si256((__m256i)(v_s2), 0) +  \
		    (__v4si)_mm256_extracti128_si256((__m256i)(v_s2), 1);   \
									    \
	ADLER32_FINISH_VEC_CHUNK_128((s1), (s2), s1_128bit, s2_128bit);	    \
}

#define ADLER32_FINISH_VEC_CHUNK_512(s1, s2, v_s1, v_s2)		    \
{									    \
	__v8si s1_256bit, s2_256bit;					    \
									    \
	/* 512 => 256 bits */						    \
	s1_256bit = (__v8si)_mm512_extracti64x4_epi64((__m512i)(v_s1), 0) + \
		    (__v8si)_mm512_extracti64x4_epi64((__m512i)(v_s1), 1);  \
	s2_256bit = (__v8si)_mm512_extracti64x4_epi64((__m512i)(v_s2), 0) + \
		    (__v8si)_mm512_extracti64x4_epi64((__m512i)(v_s2), 1);  \
									    \
	ADLER32_FINISH_VEC_CHUNK_256((s1), (s2), s1_256bit, s2_256bit);	    \
}

/* AVX-512BW implementation; like the AVX2 one but does 64 bytes at a time */
#undef DISPATCH_AVX512BW
#if !defined(DEFAULT_IMPL) &&	\
       (defined(__AVX512BW__) || (X86_CPU_FEATURES_ENABLED &&	\
				  COMPILER_SUPPORTS_AVX512BW_TARGET_INTRINSICS))
#  define FUNCNAME		adler32_avx512bw
#  define FUNCNAME_CHUNK	adler32_avx512bw_chunk
#  define IMPL_ALIGNMENT	64
#  define IMPL_SEGMENT_SIZE	64
#  define IMPL_MAX_CHUNK_SIZE	MAX_CHUNK_SIZE
#  ifdef __AVX512BW__
#    define ATTRIBUTES
#    define DEFAULT_IMPL	adler32_avx512bw
#  else
#    define ATTRIBUTES		__attribute__((target("avx512bw")))
#    define DISPATCH		1
#    define DISPATCH_AVX512BW	1
#  endif
#  include <immintrin.h>
static forceinline ATTRIBUTES void
adler32_avx512bw_chunk(const __m512i *p, const __m512i *const end,
		       u32 *s1, u32 *s2)
{
	const __m512i zeroes = _mm512_setzero_si512();
	__v16si v_s1_a = (__v16si)zeroes;
	__v16si v_s1_b = (__v16si)zeroes;
	__v16si v_s1_c = (__v16si)zeroes;
	__v16si v_s1_d = (__v16si)zeroes;
	__v16si v_s2_a = (__v16si)zeroes;
	__v16si v_s2_b = (__v16si)zeroes;
	__v16si v_s2_c = (__v16si)zeroes;
	__v16si v_s2_d = (__v16si)zeroes;

	do {
		__m128i *pp = (__m128i *)p;

		__v16si bytes0 = (__v16si)_mm512_cvtepu8_epi32(pp[0]);
		__v16si bytes1 = (__v16si)_mm512_cvtepu8_epi32(pp[1]);
		__v16si bytes2 = (__v16si)_mm512_cvtepu8_epi32(pp[2]);
		__v16si bytes3 = (__v16si)_mm512_cvtepu8_epi32(pp[3]);

		v_s2_a += v_s1_a;
		v_s2_b += v_s1_b;
		v_s2_c += v_s1_c;
		v_s2_d += v_s1_d;

		v_s1_a += bytes0;
		v_s1_b += bytes1;
		v_s1_c += bytes2;
		v_s1_d += bytes3;
	} while (++p != end);

	v_s2_a += v_s2_c;
	v_s2_b += v_s2_d;
	v_s2_a += v_s2_b;

	v_s2_a = (__v16si)_mm512_slli_epi32((__m512i)v_s2_a, 5);

	v_s2_a += v_s1_a * (__v16si){64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49};
	v_s2_a += v_s1_b * (__v16si){48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33};
	v_s2_a += v_s1_c * (__v16si){32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17};
	v_s2_a += v_s1_d * (__v16si){16, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1};

	v_s1_a += v_s1_c;
	v_s1_b += v_s1_d;
	v_s1_a += v_s1_b;

	/* Add the counters to the real s1 and s2 */
	ADLER32_FINISH_VEC_CHUNK_512(s1, s2, v_s1_a, v_s2_a);
}
#  include "../adler32_vec_template.h"
#endif /* AVX-512BW implementation */

/* AVX2 implementation; like the AVX-512BW one but does 32 bytes at a time */
#undef DISPATCH_AVX2
#if !defined(DEFAULT_IMPL) &&	\
	(defined(__AVX2__) || (X86_CPU_FEATURES_ENABLED &&	\
			       COMPILER_SUPPORTS_AVX2_TARGET_INTRINSICS))
#  define FUNCNAME		adler32_avx2
#  define FUNCNAME_CHUNK	adler32_avx2_chunk
#  define IMPL_ALIGNMENT	32
#  define IMPL_SEGMENT_SIZE	32
#  define IMPL_MAX_CHUNK_SIZE	MAX_CHUNK_SIZE
#  ifdef __AVX2__
#    define ATTRIBUTES
#    define DEFAULT_IMPL	adler32_avx2
#  else
#    define ATTRIBUTES		__attribute__((target("avx2")))
#    define DISPATCH		1
#    define DISPATCH_AVX2	1
#  endif
#  include <immintrin.h>
static forceinline ATTRIBUTES void
adler32_avx2_chunk(const __m256i *p, const __m256i *const end, u32 *s1, u32 *s2)
{
	const __m256i zeroes = _mm256_setzero_si256();
	__v8si v_s1_a = (__v8si)zeroes;
	__v8si v_s1_b = (__v8si)zeroes;
	__v8si v_s1_c = (__v8si)zeroes;
	__v8si v_s1_d = (__v8si)zeroes;
	__v8si v_s2_a = (__v8si)zeroes;
	__v8si v_s2_b = (__v8si)zeroes;
	__v8si v_s2_c = (__v8si)zeroes;
	__v8si v_s2_d = (__v8si)zeroes;

	do {
		__v8si bytes0 = (__v8si)_mm256_cvtepu8_epi32(
					_mm_loadu_si128((void *)((u8 *)p + 0)));
		__v8si bytes1 = (__v8si)_mm256_cvtepu8_epi32(
					_mm_loadu_si128((void *)((u8 *)p + 8)));
		__v8si bytes2 = (__v8si)_mm256_cvtepu8_epi32(
					_mm_loadu_si128((void *)((u8 *)p + 16)));
		__v8si bytes3 = (__v8si)_mm256_cvtepu8_epi32(
					_mm_loadu_si128((void *)((u8 *)p + 24)));

		v_s2_a += v_s1_a;
		v_s2_b += v_s1_b;
		v_s2_c += v_s1_c;
		v_s2_d += v_s1_d;

		v_s1_a += bytes0;
		v_s1_b += bytes1;
		v_s1_c += bytes2;
		v_s1_d += bytes3;
	} while (++p != end);

	v_s2_a += v_s2_c;
	v_s2_b += v_s2_d;
	v_s2_a += v_s2_b;

	v_s2_a = (__v8si)_mm256_slli_epi32((__m256i)v_s2_a, 5);

	v_s2_a += v_s1_a * (__v8si){ 32, 31, 30, 29, 28, 27, 26, 25 };
	v_s2_a += v_s1_b * (__v8si){ 24, 23, 22, 21, 20, 19, 18, 17 };
	v_s2_a += v_s1_c * (__v8si){ 16, 15, 14, 13, 12, 11, 10,  9 };
	v_s2_a += v_s1_d * (__v8si){  8,  7,  6,  5,  4,  3,  2,  1 };

	v_s1_a += v_s1_c;
	v_s1_b += v_s1_d;
	v_s1_a += v_s1_b;

	/* Add the counters to the real s1 and s2 */
	ADLER32_FINISH_VEC_CHUNK_256(s1, s2, v_s1_a, v_s2_a);
}
#  include "../adler32_vec_template.h"
#endif /* AVX2 implementation */

/* SSE2 implementation */
#undef DISPATCH_SSE2
#if !defined(DEFAULT_IMPL) &&	\
	(defined(__SSE2__) || (X86_CPU_FEATURES_ENABLED &&	\
			       COMPILER_SUPPORTS_SSE2_TARGET_INTRINSICS))
#  define FUNCNAME		adler32_sse2
#  define FUNCNAME_CHUNK	adler32_sse2_chunk
#  define IMPL_ALIGNMENT	16
#  define IMPL_SEGMENT_SIZE	32
/*
 * The 16-bit precision byte counters must not be allowed to undergo *signed*
 * overflow, otherwise the signed multiplications at the end (_mm_madd_epi16)
 * would behave incorrectly.
 */
#  define IMPL_MAX_CHUNK_SIZE	(32 * (0x7FFF / 0xFF))
#  ifdef __SSE2__
#    define ATTRIBUTES
#    define DEFAULT_IMPL	adler32_sse2
#  else
#    define ATTRIBUTES		__attribute__((target("sse2")))
#    define DISPATCH		1
#    define DISPATCH_SSE2	1
#  endif
#  include <emmintrin.h>
static forceinline ATTRIBUTES void
adler32_sse2_chunk(const __m128i *p, const __m128i *const end, u32 *s1, u32 *s2)
{
	const __m128i zeroes = _mm_setzero_si128();

	/* s1 counters: 32-bit, sum of bytes */
	__v4si v_s1 = (__v4si)zeroes;

	/* s2 counters: 32-bit, sum of s1 values */
	__v4si v_s2 = (__v4si)zeroes;

	/*
	 * Thirty-two 16-bit counters for byte sums.  Each accumulates the bytes
	 * that eventually need to be multiplied by a number 32...1 for addition
	 * into s2.
	 */
	__v8hi v_byte_sums_a = (__v8hi)zeroes;
	__v8hi v_byte_sums_b = (__v8hi)zeroes;
	__v8hi v_byte_sums_c = (__v8hi)zeroes;
	__v8hi v_byte_sums_d = (__v8hi)zeroes;

	do {
		/* Load the next 32 bytes */
		const __m128i bytes1 = *p++;
		const __m128i bytes2 = *p++;

		/*
		 * Accumulate the previous s1 counters into the s2 counters.
		 * Logically, this really should be v_s2 += v_s1 * 32, but we
		 * can do the multiplication (or left shift) later.
		 */
		v_s2 += v_s1;

		/*
		 * s1 update: use "Packed Sum of Absolute Differences" to add
		 * the bytes horizontally with 8 bytes per sum.  Then add the
		 * sums to the s1 counters.
		 */
		v_s1 += (__v4si)_mm_sad_epu8(bytes1, zeroes);
		v_s1 += (__v4si)_mm_sad_epu8(bytes2, zeroes);

		/*
		 * Also accumulate the bytes into 32 separate counters that have
		 * 16-bit precision.
		 */
		v_byte_sums_a += (__v8hi)_mm_unpacklo_epi8(bytes1, zeroes);
		v_byte_sums_b += (__v8hi)_mm_unpackhi_epi8(bytes1, zeroes);
		v_byte_sums_c += (__v8hi)_mm_unpacklo_epi8(bytes2, zeroes);
		v_byte_sums_d += (__v8hi)_mm_unpackhi_epi8(bytes2, zeroes);

	} while (p != end);

	/* Finish calculating the s2 counters */
	v_s2 = (__v4si)_mm_slli_epi32((__m128i)v_s2, 5);
	v_s2 += (__v4si)_mm_madd_epi16((__m128i)v_byte_sums_a,
				       (__m128i)(__v8hi){ 32, 31, 30, 29, 28, 27, 26, 25 });
	v_s2 += (__v4si)_mm_madd_epi16((__m128i)v_byte_sums_b,
				       (__m128i)(__v8hi){ 24, 23, 22, 21, 20, 19, 18, 17 });
	v_s2 += (__v4si)_mm_madd_epi16((__m128i)v_byte_sums_c,
				       (__m128i)(__v8hi){ 16, 15, 14, 13, 12, 11, 10, 9 });
	v_s2 += (__v4si)_mm_madd_epi16((__m128i)v_byte_sums_d,
				       (__m128i)(__v8hi){ 8,  7,  6,  5,  4,  3,  2,  1 });

	/* Add the counters to the real s1 and s2 */
	ADLER32_FINISH_VEC_CHUNK_128(s1, s2, v_s1, v_s2);
}
#  include "../adler32_vec_template.h"
#endif /* SSE2 implementation */

#ifdef DISPATCH
static inline adler32_func_t
arch_select_adler32_func(void)
{
	u32 features = get_cpu_features();

#ifdef DISPATCH_AVX512BW
	if (features & X86_CPU_FEATURE_AVX512BW)
		return adler32_avx512bw;
#endif
#ifdef DISPATCH_AVX2
	if (features & X86_CPU_FEATURE_AVX2)
		return adler32_avx2;
#endif
#ifdef DISPATCH_SSE2
	if (features & X86_CPU_FEATURE_SSE2)
		return adler32_sse2;
#endif
	return NULL;
}
#endif /* DISPATCH */
