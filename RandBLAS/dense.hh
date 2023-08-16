#ifndef randblas_dense_hh
#define randblas_dense_hh

#include "RandBLAS/base.hh"
#include "RandBLAS/exceptions.hh"
#include "RandBLAS/random_gen.hh"

#include <blas.hh>

#include <iostream>
#include <stdio.h>
#include <stdexcept>
#include <string>
#include <tuple>

#include <math.h>
#include <typeinfo>


namespace RandBLAS {
// =============================================================================
/// We call a sketching operator "dense" if it takes Level 3 BLAS work to 
/// apply to a dense matrix. All such sketching operators supported by
/// RandBLAS currently have i.i.d. entries. This enumeration specifies
/// the distribution of the entries of such a sketching operator.
enum class DenseDistName : char {
    // ---------------------------------------------------------------------------
    ///  Gaussian distribution with mean 0 and standard deviation 1
    Gaussian = 'G',

    // ---------------------------------------------------------------------------
    ///  uniform distribution over [-1, 1].
    Uniform = 'U',

    // ---------------------------------------------------------------------------
    ///  entries are defined only by a user-provided buffer
    BlackBox = 'B'
};


// =============================================================================
/// A distribution over dense sketching operators.
///
struct DenseDist {
    // ---------------------------------------------------------------------------
    ///  Matrices drawn from this distribution have this many rows.
    const int64_t n_rows;

    // ---------------------------------------------------------------------------
    ///  Matrices drawn from this distribution have this many columns.
    const int64_t n_cols;

    // ---------------------------------------------------------------------------
    ///  The distribution used for the entries of the sketching operator.
    const DenseDistName family = DenseDistName::Gaussian;

    // ---------------------------------------------------------------------------
    ///  The order in which the buffer should be populated, if sampling iid.
    const MajorAxis major_axis = MajorAxis::Long;
};


inline blas::Layout dist_to_layout(
    DenseDist D
) {
    bool is_wide = D.n_rows < D.n_cols;
    bool fa_long = D.major_axis == MajorAxis::Long;
    if (is_wide && fa_long) {
        return blas::Layout::RowMajor;
    } else if (is_wide) {
        return blas::Layout::ColMajor;
    } else if (fa_long) {
        return blas::Layout::ColMajor;
    } else {
        return blas::Layout::RowMajor;
    }
}

inline int64_t major_axis_length(
    DenseDist D
) {
    return (D.major_axis == MajorAxis::Long) ? 
        std::max(D.n_rows, D.n_cols) : std::min(D.n_rows, D.n_cols);
}

// =============================================================================
/// A sample from a prescribed distribution over dense sketching operators.
///
template <typename T, typename RNG = r123::Philox4x32>
struct DenseSkOp {

    using generator = RNG;
    using state_type = RNGState<RNG>;
    using buffer_type = T;

    /////////////////////////////////////////////////////////////////////
    //
    //      Properties specific to dense sketching operators
    //
    /////////////////////////////////////////////////////////////////////

    // ---------------------------------------------------------------------------
    ///  The distribution from which this sketching operator is sampled.
    ///  This member specifies the number of rows and columns of the sketching
    ///  operator.
    const DenseDist dist;

    // ---------------------------------------------------------------------------
    ///  The state that should be passed to the RNG when the full sketching 
    ///  operator needs to be sampled from scratch. 
    const RNGState<RNG> seed_state;

    // ---------------------------------------------------------------------------
    ///  The state that should be used by the next call to an RNG *after* the
    ///  full sketching operator has been sampled.
    RNGState<RNG> next_state;

    T *buff = nullptr;                         // memory
    const blas::Layout layout;                 // matrix storage order
    bool del_buff_on_destruct = false;         // only applies if realize_full has been called.

    /////////////////////////////////////////////////////////////////////
    //
    //      Member functions must directly relate to memory management.
    //
    /////////////////////////////////////////////////////////////////////

    //  Elementary constructor: needs an implementation
    DenseSkOp(
        DenseDist dist,
        RNGState<RNG> const& state,
        T *buff = nullptr
    );

    //  Convenience constructor (a wrapper)
    DenseSkOp(
        DenseDist dist,
        uint32_t key,
        T *buff = nullptr
    ) : DenseSkOp(dist, RNGState<RNG>(key), buff) {};

    //  Convenience constructor (a wrapper)
    DenseSkOp(
        DenseDistName family,
        int64_t n_rows,
        int64_t n_cols,
        uint32_t key,
        T *buff = nullptr,
        MajorAxis ma = MajorAxis::Long
    ) : DenseSkOp(DenseDist{n_rows, n_cols, family, ma}, RNGState<RNG>(key), buff) {};

    // Destructor
    ~DenseSkOp();
};

template <typename T, typename RNG>
DenseSkOp<T,RNG>::DenseSkOp(
    DenseDist dist,
    RNGState<RNG> const& state,
    T *buff
) : // variable definitions
    dist(dist),
    seed_state(state),
    next_state{},
    buff(buff),
    layout(dist_to_layout(dist))
{   // sanity checks
    randblas_require(this->dist.n_rows > 0);
    randblas_require(this->dist.n_cols > 0);
    if (dist.family == DenseDistName::BlackBox)
        randblas_require(this->buff != nullptr);
}

template <typename T, typename RNG>
DenseSkOp<T,RNG>::~DenseSkOp() {
    if (this->del_buff_on_destruct) {
        delete [] this->buff;
    }
}

/** 
 * Fill buff with random values so it gives a row-major representation of an n_srows \times n_scols
 * submatrix of some implicitly defined parent matrix.
 * 
 * The implicit parent matrix is **imagined** as a buffer in row-major order with "n_cols" columns.
 * "ptr" is the pointer offset for the desired submatrix in the imagined buffer of the parent matrix.
 *
 * @tparam T the data type of the matrix
 * @tparam RNG a random123 CBRNG type
 * @tparam OP an operator that transforms raw random values into matrix
 *           elements. See r123ext::uneg11 and r123ext::boxmul.
 *
 * @param[in] n_cols
 *      The number of columns in the implicitly defined parent matrix.
 * @param[in] smat
 *      A pointer to a region of memory with space for n_rows \times lda elements of type T.
 *      This memory will be filled with random values by wrting rows of length "n_scols"
 *      with an inter-row stride of length "lda".
 * @param[in] n_srows
 *      The number of rows in the submatrix.
 * @param[in] n_scols
 *      The number of columns in the submatrix.
 * @param[in] ptr
 *      The starting locaiton within the random matrix, for which 
 *      the submatrix is to be generated
 * @param[in] seed
 *      A CBRNG state
 * @param[in] lda
 *      If positive then must be >= n_scols.
 *      Otherwise, we automatically set it to n_scols.
 *
 * @returns the updated CBRNG state
 * 
 * Notes
 * -----
 * If RandBLAS is compiled with OpenMP threading support enabled, the operation is parallelized
 * using OMP_NUM_THREADS. The sequence of values generated does not depend on the number of threads.
 * 
 */
template<typename T, typename RNG, typename OP>
static RNGState<RNG> fill_dense_submat_impl(
    int64_t n_cols,
    T* smat,
    int64_t n_srows,
    int64_t n_scols,
    int64_t ptr,
    const RNGState<RNG> & seed,
    int64_t lda = 0
) {
    if (lda <= 0) {
        lda = n_scols;
    } else {
        randblas_require(lda >= n_scols);
    }
    randblas_require(n_cols >= n_scols);
    RNG rng;
    typename RNG::ctr_type c = seed.counter;
    typename RNG::key_type k = seed.key;

    int64_t i0, i1, r0, r1, s0, e1;
    int64_t prev = 0;
    int64_t i;

    #pragma omp parallel firstprivate(c, k) private(i0, i1, r0, r1, s0, e1, prev, i)
    {
    auto cc = c;
    prev = 0;
    #pragma omp for
    for (int row = 0; row < n_srows; row++) {
        int64_t ind = 0;
        i0 = ptr + row * n_cols; // start index in each row
        i1 = ptr + row * n_cols + n_scols - 1; // end index in each row
        r0 = (int64_t) i0 / RNG::ctr_type::static_size; // start counter
        r1 = (int64_t) i1 / RNG::ctr_type::static_size; // end counter
        s0 = i0 % RNG::ctr_type::static_size;
        e1 = i1 % RNG::ctr_type::static_size;

        cc.incr(r0 - prev);
        prev = r0;
        auto rv =  OP::generate(rng, cc, k);
        int64_t range = (r1 > r0)? RNG::ctr_type::static_size-1 : e1;
        for (i = s0; i <= range; i++) {
            smat[ind + row * lda] = rv[i];
            ind++;
        }

        // middle 
        int64_t tmp = r0;
        while( tmp < r1 - 1) {
            cc.incr();
            prev++;
            rv = OP::generate(rng, cc, k);
            for (i = 0; i < RNG::ctr_type::static_size; i++) {
                smat[ind + row * lda] = rv[i];
                ind++;
            }
            tmp++;
        }

        // end
        if ( r1 > r0 ) {
            cc.incr();
            prev++;
            rv = OP::generate(rng, cc, k);
            for (i = 0; i <= e1; i++) {
                smat[ind + row * lda] = rv[i];
                ind++;
            }
        }
    }

    }
    return RNGState<RNG> {c, k};
} 


template<typename T, typename RNG>
std::pair<blas::Layout, RandBLAS::RNGState<RNG>> fill_dense(
    DenseDist D,
    int64_t n_rows,
    int64_t n_cols,
    int64_t i_off,
    int64_t j_off,
    T* buff,
    const RNGState<RNG> &seed
) {

    blas::Layout layout = dist_to_layout(D);
    int64_t ma_len = major_axis_length(D);
    int64_t n_rows_, n_cols_, ptr;
    if (layout == blas::Layout::ColMajor) {
        // operate on the transpose in row-major
        n_rows_ = n_cols;
        n_cols_ = n_rows;
        ptr = i_off + j_off * ma_len;
    } else {
        n_rows_ = n_rows;
        n_cols_ = n_cols;
        ptr = i_off * ma_len + j_off;
    }
    switch (D.family) {
        case DenseDistName::Gaussian: {
            auto next_state_g = fill_dense_submat_impl<T,RNG,r123ext::boxmul>(ma_len, buff, n_rows_, n_cols_, ptr, seed);
            return std::make_tuple(layout, next_state_g);
        }
        case DenseDistName::Uniform: {
            auto next_state_u = fill_dense_submat_impl<T,RNG,r123ext::uneg11>(ma_len, buff, n_rows_, n_cols_, ptr, seed);
            return std::make_tuple(layout, next_state_u);
        }
        case DenseDistName::BlackBox:
            throw std::invalid_argument(std::string("fill_buff cannot be called with the BlackBox distribution."));
        default:
            throw std::runtime_error(std::string("Unrecognized distribution."));
    }
}
 
template <typename T, typename RNG>
std::pair<blas::Layout, RandBLAS::RNGState<RNG>> fill_dense(
    const DenseDist &D,
    T *buff,
    const RNGState<RNG> &seed
) {
    return fill_dense(D, D.n_rows, D.n_cols, 0, 0,  buff, seed);
}

template <typename T, typename RNG>
RNGState<RNG> fill_dense(
    DenseSkOp<T,RNG> &S
) {
    randblas_require(!S.buff);
    randblas_require(S.dist.family != DenseDistName::BlackBox);
    S.buff = new T[S.dist.n_rows * S.dist.n_cols];
    auto [layout, next_state] = fill_dense<T, RNG>(S.dist, S.buff, S.seed_state);
    S.next_state = next_state;
    S.del_buff_on_destruct = true;
    return next_state;
}
}  // end namespace RandBLAS

namespace RandBLAS::dense {

using namespace RandBLAS;

// =============================================================================
/// @verbatim embed:rst:leading-slashes
///
///   .. |op| mathmacro:: \operatorname{op}
///   .. |mat| mathmacro:: \operatorname{mat}
///   .. |submat| mathmacro:: \operatorname{submat}
///   .. |lda| mathmacro:: \mathrm{lda}
///   .. |ldb| mathmacro:: \mathrm{ldb}
///   .. |opA| mathmacro:: \mathrm{opA}
///   .. |opS| mathmacro:: \mathrm{opS}
///
/// @endverbatim
/// LSKGE3: Perform a GEMM-like operation
/// @verbatim embed:rst:leading-slashes
/// .. math::
///     \mat(B) = \alpha \cdot \underbrace{\op(\submat(S))}_{d \times m} \cdot \underbrace{\op(\mat(A))}_{m \times n} + \beta \cdot \underbrace{\mat(B)}_{d \times n},    \tag{$\star$}
/// @endverbatim
/// where \math{\alpha} and \math{\beta} are real scalars, \math{\op(X)} either returns a matrix \math{X}
/// or its transpose, and \math{S} is a sketching operator that takes Level 3 BLAS effort to apply.
/// 
/// @verbatim embed:rst:leading-slashes
/// What are :math:`\mat(A)` and :math:`\mat(B)`?
///     Their shapes are defined implicitly by :math:`(d, m, n, \opA)`.
///     Their precise contents are determined by :math:`(A, \lda)`, :math:`(B, \ldb)`,
///     and "layout", following the same convention as BLAS.
///
/// What is :math:`\submat(S)`?
///     Its shape is defined implicitly by :math:`(\opS, d, m)`.
///     If :math:`{\submat(S)}` is of shape :math:`r \times c`,
///     then it is the :math:`r \times c` submatrix of :math:`{S}` whose upper-left corner
///     appears at index :math:`(\texttt{i_off}, \texttt{j_off})` of :math:`{S}`.
/// @endverbatim
/// @param[in] layout
///     Layout::ColMajor or Layout::RowMajor
///      - Matrix storage for \math{\mat(A)} and \math{\mat(B)}.
///
/// @param[in] opS
///      - If \math{\opS} = NoTrans, then \math{ \op(\submat(S)) = \submat(S)}.
///      - If \math{\opS} = Trans, then \math{\op(\submat(S)) = \submat(S)^T }.
/// @param[in] opA
///      - If \math{\opA} == NoTrans, then \math{\op(\mat(A)) = \mat(A)}.
///      - If \math{\opA} == Trans, then \math{\op(\mat(A)) = \mat(A)^T}.
/// @param[in] d
///     A nonnegative integer.
///     - The number of rows in \math{\mat(B)}
///     - The number of rows in \math{\op(\mat(S))}.
///
/// @param[in] n
///     A nonnegative integer.
///     - The number of columns in \math{\mat(B)}
///     - The number of columns in \math{\op(\mat(A))}.
///
/// @param[in] m
///     A nonnegative integer.
///     - The number of columns in \math{\op(\submat(S))}
///     - The number of rows in \math{\op(\mat(A))}.
///
/// @param[in] alpha
///     A real scalar.
///     - If zero, then \math{A} is not accessed.
///
/// @param[in] S
///    A DenseSkOp object.
///    - Defines \math{\submat(S)}.
///
/// @param[in] i_off
///     A nonnegative integer.
///     - The rows of \math{\submat(S)} are a contiguous subset of rows of \math{S}.
///     - The rows of \math{\submat(S)} start at \math{S[\texttt{i_off}, :]}.
///
/// @param[in] j_off
///     A nonnnegative integer.
///     - The columns of \math{\submat(S)} are a contiguous subset of columns of \math{S}.
///     - The columns \math{\submat(S)} start at \math{S[:,\texttt{j_off}]}. 
///
/// @param[in] A
///     Pointer to a 1D array of real scalars.
///     - Defines \math{\mat(A)}.
///
/// @param[in] lda
///     A nonnegative integer.
///     * Leading dimension of \math{\mat(A)} when reading from \math{A}.
///     * If layout == ColMajor, then
///         @verbatim embed:rst:leading-slashes
///             .. math::
///                 \mat(A)[i, j] = A[i + j \cdot \lda].
///         @endverbatim
///       In this case, \math{\lda} must be \math{\geq} the length of a column in \math{\mat(A)}.
///     * If layout == RowMajor, then
///         @verbatim embed:rst:leading-slashes
///             .. math::
///                 \mat(A)[i, j] = A[i \cdot \lda + j].
///         @endverbatim
///       In this case, \math{\lda} must be \math{\geq} the length of a row in \math{\mat(A)}.
///
/// @param[in] beta
///     A real scalar.
///     - If zero, then \math{B} need not be set on input.
///
/// @param[in, out] B
///    Pointer to 1D array of real scalars.
///    - On entry, defines \math{\mat(B)}
///      on the RIGHT-hand side of \math{(\star)}.
///    - On exit, defines \math{\mat(B)}
///      on the LEFT-hand side of \math{(\star)}.
///
/// @param[in] ldb
///    - Leading dimension of \math{\mat(B)} when reading from \math{B}.
///    - Refer to documentation for \math{\lda} for details. 
///
template <typename T, typename RNG>
void lskge3(
    blas::Layout layout,
    blas::Op opS,
    blas::Op opA,
    int64_t d, // B is d-by-n
    int64_t n, // op(A) is m-by-n
    int64_t m, // op(S) is d-by-m
    T alpha,
    DenseSkOp<T,RNG> &S,
    int64_t i_off,
    int64_t j_off,
    const T *A,
    int64_t lda,
    T beta,
    T *B,
    int64_t ldb
){
    if (!S.buff) {
        // We'll make a shallow copy of the sketching operator, take responsibility for filling the memory
        // of that sketching operator, and then call LSKGE3 with that new object.
        int64_t rows_submat_S = (opS == blas::Op::NoTrans) ? d : m;
        int64_t cols_submat_S = (opS == blas::Op::NoTrans) ? m : d;
        T *buff = new T[rows_submat_S * cols_submat_S];
        fill_dense(S.dist, rows_submat_S, cols_submat_S, i_off, j_off, buff, S.seed_state);
        DenseDist D{rows_submat_S, cols_submat_S, DenseDistName::BlackBox, S.dist.major_axis};
        DenseSkOp S_(D, S.seed_state, buff);
        lskge3(layout, opS, opA, d, n, m, alpha, S_, 0, 0, A, lda, beta, B, ldb);
        delete [] buff;
        return;
    }
    bool opposing_layouts = S.layout != layout;
    if (opposing_layouts)
        opS = (opS == blas::Op::NoTrans) ? blas::Op::Trans : blas::Op::NoTrans;

    // Dimensions of A, rather than op(A)
    int64_t rows_A, cols_A, rows_submat_S, cols_submat_S;
    if (opA == blas::Op::NoTrans) {
        rows_A = m;
        cols_A = n;
    } else {
        rows_A = n;
        cols_A = m;
    }
    // Dimensions of S, rather than op(S)
    if (opS == blas::Op::NoTrans) {
        rows_submat_S = d;
        cols_submat_S = m;
    } else {
        rows_submat_S = m;
        cols_submat_S = d;
    }

    // Sanity checks on dimensions and strides
    if (opposing_layouts) {
        randblas_require(S.dist.n_rows >= cols_submat_S + i_off);
        randblas_require(S.dist.n_cols >= rows_submat_S + j_off);
    } else {
        randblas_require(S.dist.n_rows >= rows_submat_S + i_off);
        randblas_require(S.dist.n_cols >= cols_submat_S + j_off);
    }

    int64_t lds, pos;
    if (S.layout == blas::Layout::ColMajor) {
        lds = S.dist.n_rows;
        pos = i_off + lds * j_off;
    } else {
        lds = S.dist.n_cols;
        pos = i_off * lds + j_off;
    }

    if (layout == blas::Layout::ColMajor) {
        randblas_require(lda >= rows_A);
        randblas_require(ldb >= d);
    } else {
        randblas_require(lda >= cols_A);
        randblas_require(ldb >= n);
    }
    // Perform the sketch.
    blas::gemm<T>(
        layout, opS, opA,
        d, n, m,
        alpha,
        &S.buff[pos], lds,
        A, lda,
        beta,
        B, ldb
    );
    return;
}

// =============================================================================
/// RSKGE3: Perform a GEMM-like operation
/// @verbatim embed:rst:leading-slashes
/// .. math::
///     \mat(B) = \alpha \cdot \underbrace{\op(\mat(A))}_{m \times n} \cdot \underbrace{\op(\submat(S))}_{n \times d} + \beta \cdot \underbrace{\mat(B)}_{m \times d},    \tag{$\star$}
/// @endverbatim
/// where \math{\alpha} and \math{\beta} are real scalars, \math{\op(X)} either returns a matrix \math{X}
/// or its transpose, and \math{S} is a sketching operator that takes Level 3 BLAS effort to apply.
/// 
/// @verbatim embed:rst:leading-slashes
/// What are :math:`\mat(A)` and :math:`\mat(B)`?
///     Their shapes are defined implicitly by :math:`(m, d, n, \opA)`.
///     Their precise contents are determined by :math:`(A, \lda)`, :math:`(B, \ldb)`,
///     and "layout", following the same convention as BLAS.
///
/// What is :math:`\submat(S)`?
///     Its shape is defined implicitly by :math:`(\opS, n, d)`.
///     If :math:`{\submat(S)}` is of shape :math:`r \times c`,
///     then it is the :math:`r \times c` submatrix of :math:`{S}` whose upper-left corner
///     appears at index :math:`(\texttt{i_off}, \texttt{j_off})` of :math:`{S}`.
/// @endverbatim
/// @param[in] layout
///     Layout::ColMajor or Layout::RowMajor
///      - Matrix storage for \math{\mat(A)} and \math{\mat(B)}.
///
/// @param[in] opA
///      - If \math{\opA} == NoTrans, then \math{\op(\mat(A)) = \mat(A)}.
///      - If \math{\opA} == Trans, then \math{\op(\mat(A)) = \mat(A)^T}.
///
/// @param[in] opS
///      - If \math{\opS} = NoTrans, then \math{ \op(\submat(S)) = \submat(S)}.
///      - If \math{\opS} = Trans, then \math{\op(\submat(S)) = \submat(S)^T }.
///
/// @param[in] m
///     A nonnegative integer.
///     - The number of rows in \math{\mat(B)}.
///     - The number of rows in \math{\op(\mat(A))}.
///
/// @param[in] d
///     A nonnegative integer.
///     - The number of columns in \math{\mat(B)}
///     - The number of columns in \math{\op(\mat(S))}.
///
/// @param[in] n
///     A nonnegative integer.
///     - The number of columns in \math{\op(\mat(A))}
///     - The number of rows in \math{\op(\submat(S))}.
///
/// @param[in] alpha
///     A real scalar.
///     - If zero, then \math{A} is not accessed.
///
/// @param[in] A
///     Pointer to a 1D array of real scalars.
///     - Defines \math{\mat(A)}.
///
/// @param[in] lda
///     A nonnegative integer.
///     * Leading dimension of \math{\mat(A)} when reading from \math{A}.
///     * If layout == ColMajor, then
///         @verbatim embed:rst:leading-slashes
///             .. math::
///                 \mat(A)[i, j] = A[i + j \cdot \lda].
///         @endverbatim
///       In this case, \math{\lda} must be \math{\geq} the length of a column in \math{\mat(A)}.
///     * If layout == RowMajor, then
///         @verbatim embed:rst:leading-slashes
///             .. math::
///                 \mat(A)[i, j] = A[i \cdot \lda + j].
///         @endverbatim
///       In this case, \math{\lda} must be \math{\geq} the length of a row in \math{\mat(A)}.
///
/// @param[in] S
///    A DenseSkOp object.
///    - Defines \math{\submat(S)}.
///
/// @param[in] i_off
///     A nonnegative integer.
///     - The rows of \math{\submat(S)} are a contiguous subset of rows of \math{S}.
///     - The rows of \math{\submat(S)} start at \math{S[\texttt{i_off}, :]}.
///
/// @param[in] j_off
///     A nonnnegative integer.
///     - The columns of \math{\submat(S)} are a contiguous subset of columns of \math{S}.
///     - The columns \math{\submat(S)} start at \math{S[:,\texttt{j_off}]}. 
///
/// @param[in] beta
///     A real scalar.
///     - If zero, then \math{B} need not be set on input.
///
/// @param[in, out] B
///    Pointer to 1D array of real scalars.
///    - On entry, defines \math{\mat(B)}
///      on the RIGHT-hand side of \math{(\star)}.
///    - On exit, defines \math{\mat(B)}
///      on the LEFT-hand side of \math{(\star)}.
///
/// @param[in] ldb
///    - Leading dimension of \math{\mat(B)} when reading from \math{B}.
///    - Refer to documentation for \math{\lda} for details. 
///
template <typename T, typename RNG>
void rskge3(
    blas::Layout layout,
    blas::Op opA,
    blas::Op opS,
    int64_t m, // B is m-by-d
    int64_t d, // op(S) is n-by-d
    int64_t n, // op(A) is m-by-n
    T alpha,
    const T *A,
    int64_t lda,
    DenseSkOp<T,RNG> &S,
    int64_t i_off,
    int64_t j_off,
    T beta,
    T *B,
    int64_t ldb
){
    if (!S.buff) {
        // We'll make a shallow copy of the sketching operator, take responsibility for filling the memory
        // of that sketching operator, and then call RSKGE3 with that new object.
        int64_t rows_submat_S = (opS == blas::Op::NoTrans) ? n : d;
        int64_t cols_submat_S = (opS == blas::Op::NoTrans) ? d : n;
        T *buff = new T[rows_submat_S * cols_submat_S];
        fill_dense(S.dist, rows_submat_S, cols_submat_S, i_off, j_off, buff, S.seed_state);
        DenseDist D{rows_submat_S, cols_submat_S, DenseDistName::BlackBox, S.dist.major_axis};
        DenseSkOp S_(D, S.seed_state, buff);
        rskge3(layout, opA, opS, m, d, n, alpha, A, lda, S_, 0, 0, beta, B, ldb);
        delete [] buff;
        return;
    }
    bool opposing_layouts = S.layout != layout;
    if (opposing_layouts)
        opS = (opS == blas::Op::NoTrans) ? blas::Op::Trans : blas::Op::NoTrans;

    // Dimensions of A, rather than op(A)
    int64_t rows_A, cols_A, rows_submat_S, cols_submat_S;
    if (opA == blas::Op::NoTrans) {
        rows_A = m;
        cols_A = n;
    } else {
        rows_A = n;
        cols_A = m;
    }
    // Dimensions of S, rather than op(S)
    if (opS == blas::Op::NoTrans) {
        rows_submat_S = n;
        cols_submat_S = d;
    } else {
        rows_submat_S = d;
        cols_submat_S = n;
    }

    // Sanity checks on dimensions and strides
    if (opposing_layouts) {
        randblas_require(S.dist.n_rows >= cols_submat_S + i_off);
        randblas_require(S.dist.n_cols >= rows_submat_S + j_off);
    } else {
        randblas_require(S.dist.n_rows >= rows_submat_S + i_off);
        randblas_require(S.dist.n_cols >= cols_submat_S + j_off);
    }

    int64_t lds, pos;
    if (S.layout == blas::Layout::ColMajor) {
        lds = S.dist.n_rows;
        pos = i_off + lds * j_off;
    } else {
        lds = S.dist.n_cols;
        pos = i_off * lds + j_off;
    }

    if (layout == blas::Layout::ColMajor) {
        randblas_require(lda >= rows_A);
        randblas_require(ldb >= m);
    } else {
        randblas_require(lda >= cols_A);
        randblas_require(ldb >= d);
    }
    // Perform the sketch.
    blas::gemm<T>(
        layout, opA, opS,
        m, d, n,
        alpha,
        A, lda,
        &S.buff[pos], lds,
        beta,
        B, ldb
    );
    return;
}

} // end namespace RandBLAS::dense

#endif
