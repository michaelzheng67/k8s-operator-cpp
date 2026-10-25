#include "k8sop/api/detail/enum_table.hpp"

#include <cstdint>

#include <gtest/gtest.h>

namespace k8sop::api::detail {
namespace {

enum class Color : std::uint8_t { Red, Green, Blue };

// Blue is deliberately missing, to test lookups that fail.
constexpr EnumTable<Color, 2> kColors{{
    {Color::Red, "red"},
    {Color::Green, "green"},
}};

TEST(EnumTable, ToString) {
  EXPECT_EQ(enum_to_string(kColors, Color::Red), "red");
  EXPECT_EQ(enum_to_string(kColors, Color::Green), "green");
}

TEST(EnumTable, ToStringMissingIsEmpty) { EXPECT_EQ(enum_to_string(kColors, Color::Blue), ""); }

TEST(EnumTable, FromString) {
  EXPECT_EQ(enum_from_string(kColors, "red"), Color::Red);
  EXPECT_EQ(enum_from_string(kColors, "green"), Color::Green);
}

TEST(EnumTable, FromStringIsExactMatch) {
  EXPECT_EQ(enum_from_string(kColors, "blue"), std::nullopt);
  EXPECT_EQ(enum_from_string(kColors, "Red"), std::nullopt);  // case-sensitive
  EXPECT_EQ(enum_from_string(kColors, "re"), std::nullopt);   // no prefix matches
  EXPECT_EQ(enum_from_string(kColors, ""), std::nullopt);
}

TEST(EnumTable, WorksAtCompileTime) {
  // Initializing a constexpr variable forces compile-time evaluation: this only compiles if both
  // functions are usable in constant expressions. Once they work, these could be static_asserts.
  constexpr auto name = enum_to_string(kColors, Color::Green);
  constexpr auto value = enum_from_string(kColors, "red");
  EXPECT_EQ(name, "green");
  EXPECT_EQ(value, Color::Red);
}

}  // namespace
}  // namespace k8sop::api::detail
