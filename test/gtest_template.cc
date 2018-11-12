#include "gtest/gtest.h"
#include "lf/time_util.hh"
#include "lf/logger.hh"
#include "lf/masstree.hh"

using namespace lf;

namespace lf
{

class TimeTest : public ::testing::Test
{
  protected:
    virtual void SetUp()
    {
    }
    virtual void TearDown() {}

    //my_decimal d1;
};

TEST_F(TimeTest, CopyAndCompare)
{
    //EXPECT_EQ(0, uint64_2decimal(val, &d1));
}

} // namespace lf
