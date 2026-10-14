#include <gtest/gtest.h>
#include <string>

#include "app/latest_value.h"

namespace harbor::dock
{
namespace
{

TEST(LatestValue, TakeGetsTheNewestValueOnce)
{
    LatestValue<std::string> slot;
    EXPECT_FALSE(slot.Take());

    slot.Put("first");
    slot.Put("second");
    EXPECT_EQ(slot.Take(), "second");
    EXPECT_FALSE(slot.Take());
}

} // namespace
} // namespace harbor::dock
