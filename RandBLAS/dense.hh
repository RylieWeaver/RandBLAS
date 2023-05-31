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

#include <math.h>
#include <typeinfo>


namespace RandBLAS::dense {

using namespace RandBLAS::base;

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

enum class MajorAxis : char {
    // ---------------------------------------------------------------------------
    ///  short-axis vectors (cols of a wide matrix, rows of a tall matrix)
    Short = 'S',

    // ---------------------------------------------------------------------------
    ///  long-axis vectors (rows of a wide matrix, cols of a tall matrix)
    Long = 'U'
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
    const base::RNGState<RNG> seed_state;

    // ---------------------------------------------------------------------------
    ///  The state that should be used by the next call to an RNG *after* the
    ///  full sketching operator has been sampled.
    base::RNGState<RNG> next_state;

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

/** Fill a n_srows \times n_scols submatrix with random values starting at a pointer, from a n_rows \times n_cols random matrix. 
 * Assumes that the random matrix and the submatrix are row major.
 * If RandBLAS is compiled with OpenMP threading support enabled, the operation is
 * parallelized using OMP_NUM_THREADS. The sequence of values genrated is not
 * dependent on the number of OpenMP threads.
 *
 * @tparam T the data type of the matrix
 * @tparam RNG a random123 CBRNG type
 * @tparm OP an operator that transforms raw random values into matrix
 *           elements. See r123ext::uneg11 and r123ext::boxmul.
 *
 * @param[in] n_cols the number of columns in the matrix.
 * @param[in] smat a pointer to a contiguous region of memory with space for
 *                n_rows \times n_cols elements of type T. This memory will be
 *                filled with random values.
 * @param[in] n_srows the number of rows in the submatrix.
 * @param[in] n_scols the number of colomns in the submatrix.
 * @param[in] ptr the starting locaiton within the random matrix, for which 
 *                the submatrix is to be generated
 * @param[in] seed A CBRNG state
 *
 * @returns the updated CBRNG state
 */

template<typename T, typename RNG, typename OP>
auto fill_rsubmat_omp(
    int64_t n_cols,
    T* smat,
    int64_t n_srows,
    int64_t n_scols,
    int64_t ptr,
    const RNGState<RNG> & seed
) {
    RNG rng;
    typename RNG::ctr_type c = seed.counter;
    typename RNG::key_type k = seed.key;

    int64_t i0, i1, r0, r1, s0, e1;
    int64_t prev = 0;
    int64_t i, r;

    #pragma omp parallel firstprivate(c, k) private(i0, i1, r0, r1, s0, e1, prev, i, r)
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
            smat[ind + row*n_scols] = rv[i];
            ind++;
        }

        // middle 
        int64_t tmp = r0;
        while( tmp < r1 - 1) {
            cc.incr();
            prev++;
            rv = OP::generate(rng, cc, k);
            for (i = 0; i < RNG::ctr_type::static_size; i++) {
                smat[ind + row*n_scols] = rv[i];
                ind++;
            }
            tmp++;
        }

        // end
        if ( r1 > r0 ){
            cc.incr();
            prev++;
            rv = OP::generate(rng, cc, k);
            for (i = 0; i <= e1; i++) {
                smat[ind + row*n_scols] = rv[i];
                ind++;
            }
        }
    }

    }

    return RNGState<RNG> {c, k};
}  

/** Fill a n_rows \times n_cols matrix with random values. If RandBLAS is
 * compiled with OpenMP threading support enabled, the operation is
 * parallelized using OMP_NUM_THREADS. The sequence of values genrated is not
 * dependent on the number of OpenMP threads.
 *
 * @tparam T the data type of the matrix
 * @tparam RNG a random123 CBRNG type
 * @tparm OP an operator that transforms raw random values into matrix
 *           elements. See r123ext::uneg11 and r123ext::boxmul.
 *
 * @param[in] n_rows the number of rows in the matrix
 * @param[in] n_cols the number of columns in the matrix
 * @param[in] mat a pointer to a contiguous region of memory with space for
 *                n_rows \times n_cols elements of type T. This memory will be
 *                filled with random values.
 * @param[in] seed A CBRNG state
 *
 * @returns the updated CBRNG state
 */

template <typename T, typename RNG, typename OP>
RNGState<RNG> fill_rmat(
    int64_t n_rows,
    int64_t n_cols,
    T* mat,
    const RNGState<RNG> & seed,
    MajorAxis ma = MajorAxis::Long
) {

    if (ma == MajorAxis::Long && n_cols < n_rows)
        return fill_rmat<T,RNG,OP>(n_cols, n_rows, mat, seed, ma);
    if (ma == MajorAxis::Short && n_rows < n_cols)
        return fill_rmat<T,RNG,OP>(n_cols, n_rows, mat, seed, ma);

    // fill_rsubmat_omp is written from a row-major perspective.
    return fill_rsubmat_omp<T, RNG, OP>(n_cols, mat, n_rows, n_cols, 0, seed);


}
 
template <typename T, typename RNG>
auto fill_buff(
    T *buff,
    const DenseDist &D,
    RNGState<RNG> const& state
) {
    switch (D.family) {
        case DenseDistName::Gaussian:
            return fill_rmat<T,RNG,r123ext::boxmul>(D.n_rows, D.n_cols, buff, state, D.major_axis);
        case DenseDistName::Uniform:
            return fill_rmat<T,RNG,r123ext::uneg11>(D.n_rows, D.n_cols, buff, state, D.major_axis);
        case DenseDistName::BlackBox:
            throw std::invalid_argument(std::string("fill_buff cannot be called with the BlackBox distribution."));
        default:
            throw std::runtime_error(std::string("Unrecognized distribution."));
    }

    return state;
}

template <typename SKOP>
void realize_full(
    SKOP &S,
    bool del_buff_on_destruct=true
) {
    randblas_require(!S.buff);
    S.buff = new typename SKOP::buffer_type[S.dist.n_rows * S.dist.n_cols];
    S.next_state = fill_buff(S.buff, S.dist, S.seed_state);
    S.del_buff_on_destruct = del_buff_on_destruct;
}

// =============================================================================
/// @verbatim embed:rst:leading-slashes
///
///   .. |op| mathmacro:: \operatorname{op}
///   .. |mat| mathmacro:: \operatorname{mat}
///   .. |submat| mathmacro:: \operatorname{submat}
///   .. |lda| mathmacro:: \mathrm{lda}
///   .. |ldb| mathmacro:: \mathrm{ldb}
///   .. |transA| mathmacro:: \mathrm{transA}
///   .. |transS| mathmacro:: \mathrm{transS}
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
///     Their shapes are defined implicitly by :math:`(d, m, n, \transA)`.
///     Their precise contents are determined by :math:`(A, \lda)`, :math:`(B, \ldb)`,
///     and "layout", following the same convention as BLAS.
///
/// What is :math:`\submat(S)`?
///     Its shape is defined implicitly by :math:`(\transS, d, m)`.
///     If :math:`{\submat(S)}` is of shape :math:`r \times c`,
///     then it is the :math:`r \times c` submatrix of :math:`{S}` whose upper-left corner
///     appears at index :math:`(\texttt{i_os}, \texttt{j_os})` of :math:`{S}`.
/// @endverbatim
/// @param[in] layout
///     Layout::ColMajor or Layout::RowMajor
///      - Matrix storage for \math{\mat(A)} and \math{\mat(B)}.
///
/// @param[in] transS
///      - If \math{\transS} = NoTrans, then \math{ \op(\submat(S)) = \submat(S)}.
///      - If \math{\transS} = Trans, then \math{\op(\submat(S)) = \submat(S)^T }.
/// @param[in] transA
///      - If \math{\transA} == NoTrans, then \math{\op(\mat(A)) = \mat(A)}.
///      - If \math{\transA} == Trans, then \math{\op(\mat(A)) = \mat(A)^T}.
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
/// @param[in] i_os
///     A nonnegative integer.
///     - The rows of \math{\submat(S)} are a contiguous subset of rows of \math{S}.
///     - The rows of \math{\submat(S)} start at \math{S[\texttt{i_os}, :]}.
///
/// @param[in] j_os
///     A nonnnegative integer.
///     - The columns of \math{\submat(S)} are a contiguous subset of columns of \math{S}.
///     - The columns \math{\submat(S)} start at \math{S[:,\texttt{j_os}]}. 
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
    blas::Op transS,
    blas::Op transA,
    int64_t d, // B is d-by-n
    int64_t n, // op(A) is m-by-n
    int64_t m, // op(S) is d-by-m
    T alpha,
    DenseSkOp<T,RNG> &S0,
    int64_t i_os,
    int64_t j_os,
    const T *A,
    int64_t lda,
    T beta,
    T *B,
    int64_t ldb
){
    bool opposing_layouts = S0.layout != layout;
    if (opposing_layouts)
        transS = (transS == blas::Op::NoTrans) ? blas::Op::Trans : blas::Op::NoTrans;

    DenseSkOp<T,RNG> S0_shallow_copy(S0.dist, S0.seed_state, S0.buff);
    T *S0_ptr = S0.buff;
    if (!S0_ptr) {
        // The tentative RandBLAS standard doesn't let us attach memory to S0 inside this function.
        // It also requires that the results of this function are the same as if the user
        // had previously called realize_full on the sketching operator. Since the exact behavior of
        // realize_full is in flux, we call that function here as a black-box. The trouble is that
        // realize_full attaches memory to its argument. We get around this by calling realize_full
        // on a shallow copy and then getting a reference to its underlying buffer. When this function
        // exits the destructor of the shallow copy will be called and its memory will be cleaned up.
        realize_full(S0_shallow_copy);
        S0.next_state = S0_shallow_copy.next_state;
        S0_ptr = S0_shallow_copy.buff;
    }
    // Dimensions of A, rather than op(A)
    int64_t rows_A, cols_A, rows_submat_S, cols_submat_S;
    if (transA == blas::Op::NoTrans) {
        rows_A = m;
        cols_A = n;
    } else {
        rows_A = n;
        cols_A = m;
    }
    // Dimensions of S, rather than op(S)
    if (transS == blas::Op::NoTrans) {
        rows_submat_S = d;
        cols_submat_S = m;
    } else {
        rows_submat_S = m;
        cols_submat_S = d;
    }

    // Sanity checks on dimensions and strides
    int64_t lds, pos;
    if (S0.layout == blas::Layout::ColMajor) {
        lds = S0.dist.n_rows;
        if (opposing_layouts) {
            randblas_require(lds >= cols_submat_S);
        } else {
            randblas_require(lds >= rows_submat_S);
        }
        pos = i_os + lds * j_os;
    } else {
        lds = S0.dist.n_cols;
        if (opposing_layouts) {
            randblas_require(lds >= rows_submat_S);
        } else {
            randblas_require(lds >= cols_submat_S);
        }
        pos = i_os * lds + j_os;
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
        layout, transS, transA,
        d, n, m,
        alpha,
        &S0_ptr[pos], lds,
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
///     Their shapes are defined implicitly by :math:`(m, d, n, \transA)`.
///     Their precise contents are determined by :math:`(A, \lda)`, :math:`(B, \ldb)`,
///     and "layout", following the same convention as BLAS.
///
/// What is :math:`\submat(S)`?
///     Its shape is defined implicitly by :math:`(\transS, n, d)`.
///     If :math:`{\submat(S)}` is of shape :math:`r \times c`,
///     then it is the :math:`r \times c` submatrix of :math:`{S}` whose upper-left corner
///     appears at index :math:`(\texttt{i_os}, \texttt{j_os})` of :math:`{S}`.
/// @endverbatim
/// @param[in] layout
///     Layout::ColMajor or Layout::RowMajor
///      - Matrix storage for \math{\mat(A)} and \math{\mat(B)}.
///
/// @param[in] transA
///      - If \math{\transA} == NoTrans, then \math{\op(\mat(A)) = \mat(A)}.
///      - If \math{\transA} == Trans, then \math{\op(\mat(A)) = \mat(A)^T}.
///
/// @param[in] transS
///      - If \math{\transS} = NoTrans, then \math{ \op(\submat(S)) = \submat(S)}.
///      - If \math{\transS} = Trans, then \math{\op(\submat(S)) = \submat(S)^T }.
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
/// @param[in] i_os
///     A nonnegative integer.
///     - The rows of \math{\submat(S)} are a contiguous subset of rows of \math{S}.
///     - The rows of \math{\submat(S)} start at \math{S[\texttt{i_os}, :]}.
///
/// @param[in] j_os
///     A nonnnegative integer.
///     - The columns of \math{\submat(S)} are a contiguous subset of columns of \math{S}.
///     - The columns \math{\submat(S)} start at \math{S[:,\texttt{j_os}]}. 
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
    blas::Op transA,
    blas::Op transS,
    int64_t m, // B is m-by-d
    int64_t d, // op(S) is n-by-d
    int64_t n, // op(A) is m-by-n
    T alpha,
    const T *A,
    int64_t lda,
    DenseSkOp<T,RNG> &S0,
    int64_t i_os,
    int64_t j_os,
    T beta,
    T *B,
    int64_t ldb
){
    bool opposing_layouts = S0.layout != layout;
    if (opposing_layouts)
        transS = (transS == blas::Op::NoTrans) ? blas::Op::Trans : blas::Op::NoTrans;

    DenseSkOp<T,RNG> S0_shallow_copy(S0.dist, S0.seed_state, S0.buff);
    T *S0_ptr = S0.buff;
    if (!S0_ptr) {
        // The tentative RandBLAS standard doesn't let us attach memory to S0 inside this function.
        // It also requires that the results of this function are the same as if the user
        // had previously called realize_full on the sketching operator. Since the exact behavior of
        // realize_full is in flux, we call that function here as a black-box. The trouble is that
        // realize_full attaches memory to its argument. We get around this by calling realize_full
        // on a shallow copy and then getting a reference to its underlying buffer. When this function
        // exits the destructor of the shallow copy will be called and its memory will be cleaned up.
        realize_full(S0_shallow_copy);
        S0_ptr = S0_shallow_copy.buff;
    }

    // Dimensions of A, rather than op(A)
    int64_t rows_A, cols_A, rows_submat_S, cols_submat_S;
    if (transA == blas::Op::NoTrans) {
        rows_A = m;
        cols_A = n;
    } else {
        rows_A = n;
        cols_A = m;
    }
    // Dimensions of S, rather than op(S)
    if (transS == blas::Op::NoTrans) {
        rows_submat_S = n;
        cols_submat_S = d;
    } else {
        rows_submat_S = d;
        cols_submat_S = n;
    }

    // Sanity checks on dimensions and strides
    if (opposing_layouts) {
        randblas_require(S0.dist.n_rows >= cols_submat_S + i_os);
        randblas_require(S0.dist.n_cols >= rows_submat_S + j_os);
    } else {
        randblas_require(S0.dist.n_rows >= rows_submat_S + i_os);
        randblas_require(S0.dist.n_cols >= cols_submat_S + j_os);
    }

    int64_t lds, pos;
    if (S0.layout == blas::Layout::ColMajor) {
        lds = S0.dist.n_rows;
        pos = i_os + lds * j_os;
    } else {
        lds = S0.dist.n_cols;
        pos = i_os * lds + j_os;
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
        layout, transA, transS,
        m, d, n,
        alpha,
        A, lda,
        &S0_ptr[pos], lds,
        beta,
        B, ldb
    );
    return;
}

} // end namespace RandBLAS::dense

#endif
