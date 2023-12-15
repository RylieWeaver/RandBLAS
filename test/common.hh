#include "RandBLAS/config.h"
#include "RandBLAS/base.hh"
#include "RandBLAS/dense.hh"
#include "RandBLAS/util.hh"
#include "RandBLAS/test_util.hh"
#include "RandBLAS/sparse_skops.hh"
#include "RandBLAS/sparse_data/coo_matrix.hh"
#include "RandBLAS/sparse_data/csr_matrix.hh"
#include "RandBLAS/sparse_data/csc_matrix.hh"
#include <functional>


namespace test::common {

using RandBLAS::sparse_data::COOMatrix;
using RandBLAS::sparse_data::CSRMatrix;
using RandBLAS::sparse_data::CSCMatrix;
using RandBLAS::SparseSkOp;
using RandBLAS::DenseSkOp;


struct dims64_t {
    int64_t n_rows;
    int64_t n_cols;
};

template <typename T>
dims64_t dimensions(COOMatrix<T> &S) {return {S.n_rows, S.n_cols};}

template <typename T>
dims64_t dimensions(CSCMatrix<T> &S) {return {S.n_rows, S.n_cols};}

template <typename T>
dims64_t dimensions(CSRMatrix<T> &S) {return {S.n_rows, S.n_cols}; }

template <typename T>
dims64_t dimensions(SparseSkOp<T> &S) {return {S.dist.n_rows, S.dist.n_cols}; }

template <typename T>
dims64_t dimensions(DenseSkOp<T> &S) {return {S.dist.n_rows, S.dist.n_cols};}

template <typename T>
void to_explicit_buffer(COOMatrix<T> &a, T *mat_a, blas::Layout layout) {
    RandBLAS::sparse_data::coo::coo_to_dense(a, layout, mat_a);
    return;
}

template <typename T>
void to_explicit_buffer(SparseSkOp<T> &a, T *mat_a, blas::Layout layout) {
    auto a_coo = RandBLAS::sparse::coo_view_of_skop(a);
    to_explicit_buffer(a_coo, mat_a, layout);
    return;
}

template <typename T>
void to_explicit_buffer(DenseSkOp<T> &a, T *mat_a, blas::Layout layout) {
    auto n_rows = a.dist.n_rows;
    auto n_cols = a.dist.n_cols;
    int64_t stride_row = (layout == blas::Layout::ColMajor) ? 1 : n_cols;
    int64_t stride_col = (layout == blas::Layout::ColMajor) ? n_rows : 1;
    #define MAT_A(_i, _j) mat_a[(_i) * stride_row * (_j) * stride_col ]
    RandBLAS::dense::fill_dense(a);
    int64_t buff_stride_row = (a.layout == blas::Layout::ColMajor) ? 1 : n_cols;
    int64_t buff_stride_col = (a.layout == blas::Layout::ColMajor) ? n_rows : 1;
    #define BUFF(_i, _j) a.buff[(_i) * buff_stride_row + (_j) * buff_stride_col]
    for (int64_t i = 0; i < n_rows; ++i) {
        for (int64_t j = 0; j < n_cols; ++j) {
            MAT_A(i, j) = BUFF(i, j);
        }
    }
    return;
}

template <typename T, typename LinOp>
void reference_left_apply(
    blas::Layout layout,
    blas::Op transS,
    blas::Op transA,
    int64_t d, // mat(B) is d-by-n
    int64_t n, // op(mat(A)) is m-by-n
    int64_t m, // op(submat(S)) is d-by-m
    T alpha,
    LinOp &S,
    int64_t i_os,
    int64_t j_os,
    const T *A,
    int64_t lda,
    T beta,
    T *B,  // expected value produced by left_apply; compute via GEMM.
    T *E,  // allowable floating point error; apply theory + compute by GEMM.
    int64_t ldb
){
    randblas_require(d > 0);
    randblas_require(m > 0);
    randblas_require(n > 0);

    // Dimensions of mat(A), rather than op(mat(A))
    int64_t rows_mat_A, cols_mat_A, rows_submat_S, cols_submat_S;
    auto [rows_S, cols_S] = dimensions(S);
    if (transA == blas::Op::NoTrans) {
        rows_mat_A = m;
        cols_mat_A = n;
    } else {
        rows_mat_A = n;
        cols_mat_A = m;
    }
    // Dimensions of submat(S), rather than op(submat(S))
    if (transS == blas::Op::NoTrans) {
        rows_submat_S = d;
        cols_submat_S = m;
    } else {
        rows_submat_S = m;
        cols_submat_S = d;
    }
    // Sanity checks on dimensions and strides
    int64_t lds, s_row_stride, s_col_stride, pos, size_A, size_B;
    if (layout == blas::Layout::ColMajor) {
        lds = rows_S;
        pos = i_os + lds * j_os;
        randblas_require(lds >= rows_submat_S);
        randblas_require(lda >= rows_mat_A);
        randblas_require(ldb >= d);
        size_A = lda * (cols_mat_A - 1) + rows_mat_A;;
        size_B = ldb * (n - 1) + d;
        s_row_stride = 1;
        s_col_stride = lds;
    } else {
        lds = cols_S;
        pos = i_os * lds + j_os;
        randblas_require(lds >= cols_submat_S);
        randblas_require(lda >= cols_mat_A);
        randblas_require(ldb >= n);
        size_A = lda * (rows_mat_A - 1) + cols_mat_A;
        size_B = ldb * (d - 1) + n;
        s_row_stride = lds;
        s_col_stride = 1;
    }

    auto size_S = rows_S * cols_S;
    std::vector<T> S_dense(size_S);
    std::vector<T> S_dense_abs(size_S);
    to_explicit_buffer(S, S_dense.data(), layout);
    for (int64_t i = 0; i < rows_S; ++i) {
        for (int64_t j = 0; j < cols_S; ++j) {
            auto ell = i * s_row_stride + j * s_col_stride;
            S_dense_abs[ell] = abs(S_dense[ell]);
        }
    }

    // Compute the reference value
    T* S_ptr = S_dense.data();
    blas::gemm(layout, transS, transA, d, n, m,
        alpha, &S_ptr[pos], lds, A, lda, beta, B, ldb
    );

    // Compute the matrix needed for componentwise error bounds.
    std::vector<T> A_abs_vec(size_A);
    T* A_abs = A_abs_vec.data();
    for (int64_t i = 0; i < size_A; ++i)
        A_abs[i] = abs(A[i]);
    if (beta != 0.0) {
        for (int64_t i = 0; i < size_B; ++i)
            E[i] = abs(B[i]);
    }
    T eps = std::numeric_limits<T>::epsilon();
    T err_alpha = (abs(alpha) * m) * (2 * eps);
    T err_beta = abs(beta) * eps;
    T* S_abs_ptr = S_dense_abs.data();
    blas::gemm(layout, transS, transA, d, n, m,
        err_alpha, &S_abs_ptr[pos], lds, A_abs, lda, err_beta, E, ldb
    );
    return;
}

template <typename T, typename LinOp>
void reference_right_apply(
    blas::Layout layout,
    blas::Op transA,
    blas::Op transS,
    int64_t m, // B is m-by-d
    int64_t d, // op(S) is n-by-d
    int64_t n, // op(A) is m-by-n
    T alpha,
    const T *A,
    int64_t lda,
    LinOp &S0,
    int64_t i_os,
    int64_t j_os,
    T beta,
    T *B, // expected value produced by right_apply; compute via GEMM.
    T *E, // allowable floating point error; apply theory + compute by GEMM.
    int64_t ldb
) { 
    using blas::Layout;
    using blas::Op;
    //
    // Check dimensions of submat(S).
    //
    int64_t submat_S_rows, submat_S_cols;
    if (transS == Op::NoTrans) {
        submat_S_rows = n;
        submat_S_cols = d;
    } else {
        submat_S_rows = d;
        submat_S_cols = n;
    }
    randblas_require(submat_S_rows <= S0.dist.n_rows);
    randblas_require(submat_S_cols <= S0.dist.n_cols);
    //
    // Check dimensions of mat(A).
    //
    int64_t mat_A_rows, mat_A_cols;
    if (transA == Op::NoTrans) {
        mat_A_rows = m;
        mat_A_cols = n;
    } else {
        mat_A_rows = n;
        mat_A_cols = m;
    }
    if (layout == blas::Layout::ColMajor) {
        randblas_require(lda >= mat_A_rows);
    } else {
        randblas_require(lda >= mat_A_cols);
    }
    //
    // Compute B = op(A) op(submat(S)) by LSKGES. We start with the identity
    //
    //      B^T = op(submat(S))^T op(A)^T
    //
    // Then we interchange the operator "op" for op(A) and the operator (*)^T.
    //
    //      B^T = op(submat(S))^T op(A^T)
    //
    // We tell LSKGES to process (B^T) and (A^T) in the opposite memory layout
    // compared to the layout for (A, B).
    // 
    auto trans_transS = (transS == Op::NoTrans) ? Op::Trans : Op::NoTrans;
    auto trans_layout = (layout == blas::Layout::ColMajor) ? blas::Layout::RowMajor : blas::Layout::ColMajor;
    reference_left_apply(
        trans_layout, trans_transS, transA,
        d, m, n, alpha, S0, i_os, j_os, A, lda, beta, B, E, ldb
    );
}

template <typename T>
void left_apply(blas::Layout layout, blas::Op opS, blas::Op opA, int64_t d, int64_t n, int64_t m, T alpha, SparseSkOp<T> &S, int64_t row_offset, int64_t col_offset, const T *A, int64_t lda, T beta, T *B, int64_t ldb) {
    return RandBLAS::sparse::lskges(layout, opS, opA, d, n, m, alpha, S, row_offset, col_offset, A, lda, beta, B, ldb);
}

template <typename T>
void left_apply(blas::Layout layout, blas::Op opS, blas::Op opA, int64_t d, int64_t n, int64_t m, T alpha, DenseSkOp<T> &S, int64_t row_offset, int64_t col_offset, const T *A, int64_t lda, T beta, T *B, int64_t ldb) {
    return RandBLAS::dense::lskge3(layout, opS, opA, d, n, m, alpha, S, row_offset, col_offset, A, lda, beta, B, ldb);
}

template <typename T>
void left_apply(blas::Layout layout, blas::Op opS, blas::Op opA, int64_t d, int64_t n, int64_t m, T alpha, COOMatrix<T> &S, int64_t row_offset, int64_t col_offset, const T *A, int64_t lda, T beta, T *B, int64_t ldb) {
    return RandBLAS::sparse_data::coo::lspgemm(layout, opS, opA, d, n, m, alpha, S, row_offset, col_offset, A, lda, beta, B, ldb);
}

template <typename T, typename LinOp>
void test_left_apply_full_matrix(
    LinOp &S,
    int64_t n,
    blas::Layout layout,
    int threads = 0
) {
    #if !defined (RandBLAS_HAS_OpenMP)
            UNUSED(threads);
    #endif
    auto [d, m] = dimensions(S);
    T *A = new T[m * n];
    T *B0 = new T[d * n]{};
    RandBLAS::util::genmat(m, n, A, 99);
    int64_t lda, ldb;
    if (layout == blas::Layout::RowMajor) {
        lda = n; 
        ldb = n;
    } else {
        lda = m;
        ldb = d;
    }
    // compute S*A. 
    #if defined (RandBLAS_HAS_OpenMP)
        int orig_threads = omp_get_num_threads();
        if (threads > 0)
            omp_set_num_threads(threads);
    #endif
    left_apply<T>(
        layout, blas::Op::NoTrans, blas::Op::NoTrans,
        d, n, m,
        1.0, S, 0, 0, A, lda,
        0.0, B0, ldb 
    );
    #if defined (RandBLAS_HAS_OpenMP)
        omp_set_num_threads(orig_threads);
    #endif

    // compute expected result (B1) and allowable error (E)
    T *B1 = new T[d * n]{};
    T *E = new T[d * n]{};
    reference_left_apply<T>(
        layout, blas::Op::NoTrans, blas::Op::NoTrans,
        d, n, m,
        1.0, S, 0, 0, A, lda,
        0.0, B1, E, ldb
    );

    // check the result
    RandBLAS_Testing::Util::buffs_approx_equal<T>(
        B0, B1, E, d * n,
        __PRETTY_FUNCTION__, __FILE__, __LINE__
    );

    delete [] A;
    delete [] B0;
    delete [] B1;
    delete [] E;
}

template <typename T, typename LinOp>
static void test_left_apply_submatrix(
    LinOp &S0,
    int64_t d1, // rows in sketch
    int64_t m1, // size of identity matrix
    int64_t S_ro, // row offset for S in S0
    int64_t S_co, // column offset for S in S0
    blas::Layout layout,
    int threads = 0
) {
    auto [d0, m0] = dimensions(S0);
    assert(d0 >= d1);
    assert(m0 >= m1);
    bool is_colmajor = layout == blas::Layout::ColMajor;
    int64_t pos = (is_colmajor) ? (S_ro + d0 * S_co) : (S_ro * m0 + S_co);
    assert(d0 * m0 >= pos + d1 * m1);

    T *S0_dense = new T[d0 * m0];
    to_explicit_buffer(S0, S0_dense, layout);
    int64_t lda, ldb, lds0;
    if (is_colmajor) {
        lda = m1;
        ldb = d1;
        lds0 = d0;
    } else {
        lda = m1; 
        ldb = m1;
        lds0 = m0;
    }

    // define a matrix to be sketched, and create workspace for sketch.
    std::vector<T> A(m1 * m1, 0.0);
    for (int i = 0; i < m1; ++i)
        A[i + m1*i] = 1.0;
    std::vector<T> B(d1 * m1, 0.0);
    
    // Perform the sketch
    #if defined (RandBLAS_HAS_OpenMP)
        int orig_threads = omp_get_num_threads();
        if (threads > 0)
            omp_set_num_threads(1);
    #endif
    left_apply(
        layout,
        blas::Op::NoTrans,
        blas::Op::NoTrans,
        d1, m1, m1,
        1.0, S0, S_ro, S_co,
        A.data(), lda,
        0.0, B.data(), ldb   
    );
    #if defined (RandBLAS_HAS_OpenMP)
        omp_set_num_threads(orig_threads);
    #endif

    // Check the result
    RandBLAS_Testing::Util::matrices_approx_equal(
        layout, blas::Op::NoTrans,
        d1, m1,
        B.data(), ldb,
        &S0_dense[pos], lds0,
        __PRETTY_FUNCTION__, __FILE__, __LINE__
    );

    delete [] S0_dense;
}

template <typename T, typename LinOp>
void test_left_apply_alpha_beta(
    LinOp &S,
    T alpha, 
    T beta,
    blas::Layout layout
) {
    auto [d, m] = dimensions(S);
    bool is_colmajor = (layout == blas::Layout::ColMajor);
    // define a matrix to be sketched
    std::vector<T> A(m * m, 0.0);
    for (int i = 0; i < m; ++i)
        A[i + m*i] = 1.0;

    // create initialized workspace for the sketch
    std::vector<T> B0(d * m);
    RandBLAS::dense::DenseDist DB(d, m);
    RandBLAS::dense::fill_dense(DB, B0.data(), RandBLAS::RNGState(42));
    int64_t ldb = (is_colmajor) ? d : m;
    std::vector<T> B1(d * m);
    blas::copy(d * m, B0.data(), 1, B1.data(), 1);

    // perform the sketch
    left_apply<T>(
        layout,
        blas::Op::NoTrans,
        blas::Op::NoTrans,
        d, m, m,
        alpha, S, 0, 0,
        A.data(), m,
        beta, B0.data(), ldb
    );

    // compute the reference result (B1) and error bound (E).
    std::vector<T> E(d * m, 0.0);
    reference_left_apply<T>(
        layout,
        blas::Op::NoTrans,
        blas::Op::NoTrans,
        d, m, m,
        alpha, S, 0, 0,
        A.data(), m,
        beta, B1.data(), E.data(), ldb
    );

    RandBLAS_Testing::Util::buffs_approx_equal(
        B0.data(), B1.data(), E.data(), d * m,
        __PRETTY_FUNCTION__, __FILE__, __LINE__
    );
}


template <typename T, typename LinOp>
void test_left_apply_to_eye(
    LinOp &S,
    blas::Layout layout,
    int threads = 0
) {
    #if !defined (RandBLAS_HAS_OpenMP)
            UNUSED(threads);
    #endif
    auto [d, m] = dimensions(S);
    T *B = new T[d * m];
    T *A = new T[m * m]{};
    for (int i = 0; i < m; ++i)
        A[i + m*i] = 1.0;
    // A = eye(m)
    int64_t ldb = (layout == blas::Layout::ColMajor) ? d : m;

    // compute S * eye(m)
    #if defined (RandBLAS_HAS_OpenMP)
        int orig_threads = omp_get_num_threads();
        if (threads > 0)
            omp_set_num_threads(threads);
    #endif
    left_apply<T>(
        layout, blas::Op::NoTrans, blas::Op::NoTrans,
        d, m, m, 1.0, S, 0, 0, A, m,  0.0, B, ldb 
    );
    #if defined (RandBLAS_HAS_OpenMP)
        omp_set_num_threads(orig_threads);
    #endif

    // check the result
    auto size_S = d * m;
    std::vector<T> S_dense(size_S);
    to_explicit_buffer(S, S_dense.data(), layout);
    RandBLAS_Testing::Util::buffs_approx_equal<T>(
        B, S_dense.data(), size_S,
        __PRETTY_FUNCTION__, __FILE__, __LINE__
    );

    delete [] A;
    delete [] B;
}

} // end namespace test::common

