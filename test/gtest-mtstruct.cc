#include "gtest/gtest.h"
#include "lf/logger.hh"
#include "lf/masstree.hh"

using namespace lf;

namespace lf
{

class MtStructTest : public ::testing::Test
{
  protected:
    virtual void SetUp()
    {
        lf::g_stdout_logger_on = true;
        lf::g_all_threads = new std::vector<lf::ThreadInfo>(1);
        ti_ = &((*lf::g_all_threads)[0]);
    }
    virtual void TearDown()
    {
        ti_ = nullptr;
        delete lf::g_all_threads;
    }

    ThreadInfo *ti_;
};

TEST_F(MtStructTest, MakeComparable)
{
    //uint64_t k = net_to_host_order((uint64_t)1);
    //lf::log("k %lld", k);

    char s[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};
    uint64_t j = StringSlice::make_comparable(s, 0);
    EXPECT_EQ(j, 0);

    for (int i = 1; i <= 8; i++)
    {
        uint64_t oj = j;
        j = StringSlice::make_comparable(s, i);
        EXPECT_EQ(j, oj | uint64_t(i) << (8 * (8 - i)));
        char buf[8] = {0};
        StringSlice::unparse_comparable(buf, 8, j, i);
        EXPECT_EQ(memcmp(buf, s, i), 0);
    }

    j = StringSlice::make_comparable(s, 9);
    EXPECT_EQ(j, StringSlice::make_comparable(s, 8));

    j = StringSlice::make_comparable(s, 1);
    EXPECT_EQ(j, ((uint64_t)1) << (8 * 7));

    j = StringSlice::make_comparable(s, 2);
    EXPECT_EQ(j, (uint64_t(1) << 56) | (uint64_t(2) << 48));

    j = StringSlice::make_comparable(s, 3);
    EXPECT_EQ(j, (uint64_t(1) << 56) | (uint64_t(2) << 48) | (uint64_t(3) << 40));

    char a[] = "1234567890";
    char b[] = "1234098765";
    ASSERT_TRUE(StringSlice::equals_sloppy(a, b, 1));
    ASSERT_TRUE(StringSlice::equals_sloppy(a, b, 2));
    ASSERT_TRUE(StringSlice::equals_sloppy(a, b, 3));
    ASSERT_TRUE(StringSlice::equals_sloppy(a, b, 4));
    ASSERT_FALSE(StringSlice::equals_sloppy(a, b, 5));
    ASSERT_FALSE(StringSlice::equals_sloppy(a, b, 6));
    ASSERT_FALSE(StringSlice::equals_sloppy(a, b, 10));
}



} // namespace lf
