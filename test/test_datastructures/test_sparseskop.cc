// Copyright, 2024. See LICENSE for copyright holder information.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// (1) Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// (2) Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// (3) Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

#include <RandBLAS/dense_skops.hh>
#include <RandBLAS/sparse_skops.hh>
#include <RandBLAS/util.hh>
#include "test/comparison.hh"
#include <gtest/gtest.h>
#include <math.h>


class TestSparseSkOpConstruction : public ::testing::Test
{
    protected:
        std::vector<uint32_t> keys{42, 0, 1};
        std::vector<int64_t> vec_nnzs{(int64_t) 1, (int64_t) 2, (int64_t) 3, (int64_t) 7};     
    
    virtual void SetUp() {};

    virtual void TearDown() {};

    template <typename SKOP>
    void check_fixed_nnz_per_col(SKOP &S0) {
        using sint_t = typename SKOP::index_t;
        std::set<sint_t> s;
        for (int64_t i = 0; i < S0.dist.n_cols; ++i) {
            int64_t offset = S0.dist.vec_nnz * i;
            s.clear();
            for (int64_t j = 0; j < S0.dist.vec_nnz; ++j) {
                sint_t row = S0.rows[offset + j];
                ASSERT_EQ(s.count(row), 0) << "row index " << row << " was duplicated in column " << i << std::endl;
                s.insert(row);
            }
        }
    }

    template <typename SKOP>
    void check_fixed_nnz_per_row(SKOP &S0) {
        using sint_t = typename SKOP::index_t;
        std::set<sint_t> s;
        for (int64_t i = 0; i < S0.dist.n_rows; ++i) {
            int64_t offset = S0.dist.vec_nnz * i;
            s.clear();
            for (int64_t j = 0; j < S0.dist.vec_nnz; ++j) {
                sint_t col = S0.cols[offset + j];
                ASSERT_EQ(s.count(col), 0)  << "column index " << col << " was duplicated in row " << i << std::endl;
                s.insert(col);
            }
        }
    }

    template <RandBLAS::SignedInteger sint_t>
    void proper_saso_construction(int64_t d, int64_t m, int64_t key_index, int64_t nnz_index) {
        using RNG = RandBLAS::SparseSkOp<float>::state_t::generator;
        RandBLAS::SparseSkOp<float, RNG, sint_t> S0(
            {d, m, vec_nnzs[nnz_index], RandBLAS::MajorAxis::Short}, keys[key_index]
        );
        RandBLAS::fill_sparse(S0);
        if (d < m) {
                check_fixed_nnz_per_col(S0);
        } else {
                check_fixed_nnz_per_row(S0);
        }
    }

    template <RandBLAS::SignedInteger sint_t>
    void proper_laso_construction(int64_t d, int64_t m, int64_t key_index, int64_t nnz_index) {
        using RNG = RandBLAS::SparseSkOp<float>::state_t::generator;
        RandBLAS::SparseSkOp<float, RNG, sint_t> S0(
            {d, m, vec_nnzs[nnz_index], RandBLAS::MajorAxis::Long}, keys[key_index]
        );
        RandBLAS::fill_sparse(S0);
        if (d < m) {
                check_fixed_nnz_per_row(S0);
        } else {
                check_fixed_nnz_per_col(S0);
        } 
    }
};


////////////////////////////////////////////////////////////////////////
//
//
//     SASOs
//
//
////////////////////////////////////////////////////////////////////////

TEST_F(TestSparseSkOpConstruction, SASO_Dim_7by20) {
    // vec_nnz=1
    proper_saso_construction<int64_t>(7, 20, 0, 0);
    proper_saso_construction<int64_t>(7, 20, 1, 0);
    proper_saso_construction<int64_t>(7, 20, 2, 0);
    // vec_nnz=2
    proper_saso_construction<int64_t>(7, 20, 0, 1);
    proper_saso_construction<int64_t>(7, 20, 1, 1);
    proper_saso_construction<int64_t>(7, 20, 2, 1);
    // vec_nnz=3
    proper_saso_construction<int64_t>(7, 20, 0, 2);
    proper_saso_construction<int64_t>(7, 20, 1, 2);
    proper_saso_construction<int64_t>(7, 20, 2, 2);
    // vec_nnz=7
    proper_saso_construction<int64_t>(7, 20, 0, 3);
    proper_saso_construction<int64_t>(7, 20, 1, 3);
    proper_saso_construction<int64_t>(7, 20, 2, 3);
}


TEST_F(TestSparseSkOpConstruction, SASO_Dim_15by7) {
    // vec_nnz=1
    proper_saso_construction<int64_t>(15, 7, 0, 0);
    proper_saso_construction<int64_t>(15, 7, 1, 0);
    // vec_nnz=1
    proper_saso_construction<int64_t>(15, 7, 0, 1);
    proper_saso_construction<int64_t>(15, 7, 1, 1);
    // vec_nnz=3
    proper_saso_construction<int64_t>(15, 7, 0, 2);
    proper_saso_construction<int64_t>(15, 7, 1, 2);
    // vec_nnz=7
    proper_saso_construction<int64_t>(15, 7, 0, 3);
    proper_saso_construction<int64_t>(15, 7, 1, 3);
}


TEST_F(TestSparseSkOpConstruction, SASO_Dim_7by20_int32) {
    // test vec_nnz = 1, 2, 3, 7
    proper_saso_construction<int>(7, 20, 0, 0);
    proper_saso_construction<int>(7, 20, 0, 1);
    proper_saso_construction<int>(7, 20, 0, 2);
    proper_saso_construction<int>(7, 20, 0, 3);
}


TEST_F(TestSparseSkOpConstruction, SASO_Dim_15by7_int32) {
    // test vec_nnz = 1, 2, 3, 7
    proper_saso_construction<int>(15, 7, 0, 0);
    proper_saso_construction<int>(15, 7, 0, 1);
    proper_saso_construction<int>(15, 7, 0, 2);
    proper_saso_construction<int>(15, 7, 0, 3);
}


////////////////////////////////////////////////////////////////////////
//
//
//     LASOs
//
//
////////////////////////////////////////////////////////////////////////

TEST_F(TestSparseSkOpConstruction, LASO_Dim_7by20) {
    // vec_nnz=1
    proper_laso_construction<int64_t>(7, 20, 0, 0);
    proper_laso_construction<int64_t>(7, 20, 1, 0);
    proper_laso_construction<int64_t>(7, 20, 2, 0);
    // vec_nnz=2
    proper_laso_construction<int64_t>(7, 20, 0, 1);
    proper_laso_construction<int64_t>(7, 20, 1, 1);
    proper_laso_construction<int64_t>(7, 20, 2, 1);
    // vec_nnz=3
    proper_laso_construction<int64_t>(7, 20, 0, 2);
    proper_laso_construction<int64_t>(7, 20, 1, 2);
    proper_laso_construction<int64_t>(7, 20, 2, 2);
    // vec_nnz=7
    proper_laso_construction<int64_t>(7, 20, 0, 3);
    proper_laso_construction<int64_t>(7, 20, 1, 3);
    proper_laso_construction<int64_t>(7, 20, 2, 3);
}


TEST_F(TestSparseSkOpConstruction, LASO_Dim_15by7) {
    // vec_nnz=1
    proper_laso_construction<int64_t>(15, 7, 0, 0);
    proper_laso_construction<int64_t>(15, 7, 1, 0);
    // vec_nnz=2
    proper_laso_construction<int64_t>(15, 7, 0, 1);
    proper_laso_construction<int64_t>(15, 7, 1, 1);
    // vec_nnz=3
    proper_laso_construction<int64_t>(15, 7, 0, 2);
    proper_laso_construction<int64_t>(15, 7, 1, 2);
    // vec_nnz=7
    proper_laso_construction<int64_t>(15, 7, 0, 3);
    proper_laso_construction<int64_t>(15, 7, 1, 3);
}


TEST_F(TestSparseSkOpConstruction, LASO_Dim_7by20_int32) {
    // vec_nnz=1
    proper_laso_construction<int>(7, 20, 0, 0);
    proper_laso_construction<int>(7, 20, 1, 0);
    proper_laso_construction<int>(7, 20, 2, 0);
    // vec_nnz=2
    proper_laso_construction<int>(7, 20, 0, 1);
    proper_laso_construction<int>(7, 20, 1, 1);
    proper_laso_construction<int>(7, 20, 2, 1);
    // vec_nnz=3
    proper_laso_construction<int>(7, 20, 0, 2);
    proper_laso_construction<int>(7, 20, 1, 2);
    proper_laso_construction<int>(7, 20, 2, 2);
    // vec_nnz=7
    proper_laso_construction<int>(7, 20, 0, 3);
    proper_laso_construction<int>(7, 20, 1, 3);
    proper_laso_construction<int>(7, 20, 2, 3);
}


TEST_F(TestSparseSkOpConstruction, LASO_Dim_15by7_int32) {
    // vec_nnz=1
    proper_laso_construction<int>(15, 7, 0, 0);
    proper_laso_construction<int>(15, 7, 1, 0);
    // vec_nnz=2
    proper_laso_construction<int>(15, 7, 0, 1);
    proper_laso_construction<int>(15, 7, 1, 1);
    // vec_nnz=3
    proper_laso_construction<int>(15, 7, 0, 2);
    proper_laso_construction<int>(15, 7, 1, 2);
    // vec_nnz=7
    proper_laso_construction<int>(15, 7, 0, 3);
    proper_laso_construction<int>(15, 7, 1, 3);
}
