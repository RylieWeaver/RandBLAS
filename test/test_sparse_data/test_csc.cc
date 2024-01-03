#include "test/test_sparse_data/common.hh"
#include "../comparison.hh"
#include <gtest/gtest.h>
#include <algorithm>

using namespace RandBLAS::sparse_data;
using namespace RandBLAS::sparse_data::csc;
using namespace test::sparse_data::common;
using blas::Layout;



class TestCSC_Conversions : public ::testing::Test
{
    protected:
    
    virtual void SetUp(){};

    virtual void TearDown(){};

    template <typename T = double>
    static void test_csc_from_random_sparsified(Layout layout, int64_t m, int64_t n, T p) {
        // Step 1. get dense representation of random sparse matrix
        RandBLAS::RNGState s(0);
        auto dn_mat = new T[m * n];
        iid_sparsify_random_dense(m, n, layout, dn_mat, p, s);

        // Step 2. convert the dense representation into a CSR matrix
        CSCMatrix<T> spmat(m, n, IndexBase::Zero);
        dense_to_csc(layout, dn_mat, 0.0, spmat);

        // Step 3. reconstruct the dense representation of dn_mat from the CSR matrix.
        auto dn_mat_recon = new T[m * n];
        csc_to_dense(spmat, layout, dn_mat_recon);

        // check equivalence of dn_mat and dn_mat_recon
        test::comparison::buffs_approx_equal(dn_mat, dn_mat_recon, m * n,
            __PRETTY_FUNCTION__, __FILE__, __LINE__
        );

        delete [] dn_mat;
        delete [] dn_mat_recon;
    }

    template <typename T = double>
    static void test_csc_from_diag_coo(int64_t m, int64_t n, int64_t offset) {
        int64_t len = (offset >= 0) ? std::min(m, n - offset) : std::min(m + offset, n);
        randblas_require(len > 0);
        T *diag = new T[len]{0.0};
        for (int i = 1; i <= len; ++i)
            diag[i-1] = (T) i * 0.5;
        T *mat_expect = new T[m * n]{0.0};
        #define MAT_EXPECT(_i, _j) mat_expect[(_i) + m*(_j)]
        if (offset >= 0) {
            for (int64_t ell = 0; ell < len; ++ell)
                MAT_EXPECT(ell, ell + offset) = diag[ell];
        } else {
            for (int64_t ell = 0; ell < len; ++ell)
                MAT_EXPECT(ell - offset, ell) = diag[ell];
        }

        CSCMatrix<T> csc(m, n);
        COOMatrix<T> coo(m, n);
        coo_from_diag(diag, len, offset, coo);
        coo_to_csc(coo, csc);
        T *mat_actual = new T[m * n]{0.0};
        csc_to_dense(csc, Layout::ColMajor, mat_actual);

        test::comparison::matrices_approx_equal(
            Layout::ColMajor, Layout::ColMajor, blas::Op::NoTrans,
            m, n, mat_expect, m, mat_actual, m,
            __PRETTY_FUNCTION__, __FILE__, __LINE__
        );

        delete [] mat_expect;
        delete [] diag;
        delete [] mat_actual;
        return;
    }
};
 
TEST_F(TestCSC_Conversions, dense_random_rowmajor) {
    test_csc_from_random_sparsified(Layout::RowMajor, 10, 5, 0.7);
}

TEST_F(TestCSC_Conversions, dense_random_colmajor) {
    test_csc_from_random_sparsified(Layout::ColMajor, 10, 5, 0.7);
}

TEST_F(TestCSC_Conversions, coo_diagonal_square_zero_offset) {
    test_csc_from_diag_coo(5, 5, 0);
}

TEST_F(TestCSC_Conversions, coo_diagonal_square_pos_offset) {
    test_csc_from_diag_coo(5, 5, 1);
    test_csc_from_diag_coo(5, 5, 2);
    test_csc_from_diag_coo(5, 5, 3);
    test_csc_from_diag_coo(5, 5, 4);
}

TEST_F(TestCSC_Conversions, coo_diagonal_square_neg_offset) {
    test_csc_from_diag_coo(5, 5, -1);
    test_csc_from_diag_coo(5, 5, -2);
    test_csc_from_diag_coo(5, 5, -3);
    test_csc_from_diag_coo(5, 5, -4);
}

TEST_F(TestCSC_Conversions, coo_diagonal_rectangular_zero_offset) {
    test_csc_from_diag_coo(5, 10, 0);
    test_csc_from_diag_coo(10, 5, 0);
}

TEST_F(TestCSC_Conversions, coo_diagonal_rectangular_pos_offset) {
    test_csc_from_diag_coo(10, 5, 1);
    test_csc_from_diag_coo(10, 5, 2);
    test_csc_from_diag_coo(10, 5, 3);
    test_csc_from_diag_coo(10, 5, 4);
    test_csc_from_diag_coo(5, 10, 1);
    test_csc_from_diag_coo(5, 10, 2);
    test_csc_from_diag_coo(5, 10, 3);
    test_csc_from_diag_coo(5, 10, 4);
}

TEST_F(TestCSC_Conversions, coo_diagonal_rectangular_neg_offset) {
    test_csc_from_diag_coo(10, 5, -1);
    test_csc_from_diag_coo(10, 5, -2);
    test_csc_from_diag_coo(10, 5, -3);
    test_csc_from_diag_coo(10, 5, -4);
    test_csc_from_diag_coo(5, 10, -1);
    test_csc_from_diag_coo(5, 10, -2);
    test_csc_from_diag_coo(5, 10, -3);
    test_csc_from_diag_coo(5, 10, -4);
 }

