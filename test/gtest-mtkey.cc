#include "gtest/gtest.h"
#include "lf/logger.hh"
#include "lf/masstree.hh"

using namespace lf;

namespace lf
{

class MtKeyTest : public ::testing::Test
{
  protected:
    virtual void SetUp()
    {
        lf::g_stdout_logger_on = true;
    }
    virtual void TearDown() {}
};

TEST_F(MtKeyTest, MakeComparable)
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

TEST_F(MtKeyTest, con)
{
    for (int i = 0; i < 8; i++)
    {
        MtKey mk(uint64_t(1) << (8 * i));
        EXPECT_EQ(mk.length(), 8 - i);
    }

    EXPECT_TRUE(MtKey(uint64_t(0)).empty());
}

TEST_F(MtKeyTest, shift)
{
    char s[] = "12345678909876543210";
    MtKey mk1(s, sizeof(s));
    EXPECT_EQ(mk1.ikey(), MtKey(s, 8).ikey());
    EXPECT_TRUE(mk1.has_suffix());
    EXPECT_EQ(mk1.suffix(), Slice(s + 8, sizeof(s) - 8));

    mk1.shift();
    EXPECT_EQ(mk1.suffix(), Slice(s + 16, sizeof(s) - 16));
    EXPECT_TRUE(mk1.is_shifted());
    EXPECT_EQ(mk1.length(), sizeof(s) - 8);

    mk1.shift();
    EXPECT_FALSE(mk1.has_suffix());
    EXPECT_EQ(mk1.length(), sizeof(s) - 16);

    mk1.unshift_all();
    EXPECT_EQ(mk1.full_string(), Slice(s, sizeof(s)));
}

TEST_F(MtKeyTest, compare)
{
    char s[] = {1, 2, 3, 4, 5,
                6, 7, 8, 9, 10,
                11, 12, 13, 14, 15};
    MtKey mk1(s, 9);
    EXPECT_EQ(mk1.compare(MtKey(s, 10)), 0);
    EXPECT_EQ(mk1.compare(MtKey(s, 9)), 0);
    EXPECT_TRUE(mk1.compare(MtKey(s, 8)) > 0);
    EXPECT_TRUE(mk1.compare(MtKey(s, 7)) > 0);

    char s1[] = {1, 2, 3, 4, 5, 0, 0, 0,
                 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_TRUE(MtKey(s1, 5).compare(MtKey(s1, 4)) > 0);
    EXPECT_TRUE(MtKey(s1, 5).compare(MtKey(s1, 5)) == 0);
    EXPECT_TRUE(MtKey(s1, 5).compare(MtKey(s1, 6)) < 0);
    EXPECT_TRUE(MtKey(s1, 8).compare(MtKey(s1, 9)) < 0);

    EXPECT_TRUE(MtKey(0).compare(0, 1) < 0);
    EXPECT_TRUE(MtKey(1, 8).compare(1, 9) < 0);
}

TEST_F(MtKeyTest, unparse)
{
    char s[] = "12345678901234567";
    MtKey mk1(s, sizeof(s));
    std::string buf;
    EXPECT_EQ(mk1.unparse(buf), Slice(s, sizeof(s)));

    mk1.shift();
    EXPECT_EQ(mk1.unparse(buf), Slice(s+8, sizeof(s)-8));

    mk1.shift();
    EXPECT_EQ(mk1.unparse(buf), Slice(s+16, sizeof(s)-16));
}

TEST_F(MtKeyTest, assign)
{
    char s[] = "1234567890";
    char t[] = "9876543210";
    char d[] = "9876543290";
    std::string buf;
    MtKey mk1(s, 10);
    MtKey mk2(t, 10);
    mk1.assign_store_ikey(mk2.ikey());
    EXPECT_EQ(mk1.ikey(), mk2.ikey());
    EXPECT_EQ(mk1.unparse(buf), Slice(d, 10));
}

} // namespace lf
