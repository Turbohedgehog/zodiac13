#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/component_codec.h>
#include <lib_core/component_meta.h>

namespace {

namespace ft = z13::flecs_tools;

enum class Mood : int32_t { kCalm, kAngry };

struct Everything {
  bool flag {};
  int8_t small {};
  uint16_t port {};
  int64_t big {};
  uint64_t huge {};
  uintptr_t address {};
  float ratio {};
  double precise {};
  Mood mood {};
  std::string text;
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
};

struct Flags {
  uint32_t bits {};
};

constexpr uint32_t kFlagA = 1;
constexpr uint32_t kFlagB = 2;
constexpr uint32_t kUnknownFlag = 4;
constexpr int32_t kUnknownMood = 7;

struct Link {
  flecs::entity_t target {};
};

struct Cells {
  std::array<int32_t, 3> values {};
};

constexpr int32_t kCellCount = 3;

template <class T>
std::span<const std::byte> BytesOf(const T& value) {
  return std::as_bytes(std::span(&value, 1));
}

template <class T>
std::span<std::byte> MutableBytesOf(T& value) {
  return std::as_writable_bytes(std::span(&value, 1));
}
constexpr std::size_t kMatrixCountOffset = 0;

class ComponentCodecTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ft::RegisterStdStringMeta(world_);
    ft::RegisterEigenMeta(world_);
    ft::RegisterComponentMeta<Everything>(world_);
    // Beyond RegisterComponentMeta: a bitmask, an entity member (it would become u64) and an inline array.
    world_.component<Flags>().bit("kFlagA", kFlagA).bit("kFlagB", kFlagB);
    world_.component<Link>().member(flecs::Entity, "target");
    world_.component<Cells>().member<int32_t>("values", kCellCount, offsetof(Cells, values));
    target_ = world_.entity("some::target");
  }

  Everything Sample() const {
    Everything value;
    value.flag = true;
    value.small = -7;
    value.port = 54321;
    value.big = -(int64_t{1} << 40);
    value.huge = ~uint64_t{0};
    value.address = 0xBEEF;
    value.ratio = 1.f / 3.f;
    value.precise = -2.5e-300;
    value.mood = Mood::kAngry;
    value.text = "hello\nworld";
    value.transform(0, 3) = 12345.678f;
    value.transform(2, 1) = -0.1f;
    return value;
  }

  std::vector<uint8_t> Encode(const Everything& value) const {
    return ft::EncodeValue(world_, world_.component<Everything>(), BytesOf(value)).value();
  }

  flecs::world world_;
  flecs::entity target_;
};

TEST_F(ComponentCodecTest, EveryFieldKindRoundTrips) {
  const Everything original = Sample();

  Everything restored;
  ASSERT_TRUE(ft::DecodeValue(world_, world_.component<Everything>(), MutableBytesOf(restored), Encode(original)));

  EXPECT_EQ(restored.flag, original.flag);
  EXPECT_EQ(restored.small, original.small);
  EXPECT_EQ(restored.port, original.port);
  EXPECT_EQ(restored.big, original.big);
  EXPECT_EQ(restored.huge, original.huge);
  EXPECT_EQ(restored.address, original.address);
  EXPECT_EQ(restored.ratio, original.ratio);
  EXPECT_EQ(restored.precise, original.precise);
  EXPECT_EQ(restored.mood, original.mood);
  EXPECT_EQ(restored.text, original.text);
  EXPECT_EQ(restored.transform, original.transform);
}

TEST_F(ComponentCodecTest, RejectsMemoryOfTheWrongSize) {
  const Cells cells;
  const flecs::entity_t type = world_.component<Everything>();

  EXPECT_FALSE(ft::EncodeValue(world_, type, BytesOf(cells)));
}

TEST_F(ComponentCodecTest, InlineArrayMemberRoundTrips) {
  const Cells original {.values = {1, -2, 3}};
  const auto bytes = ft::EncodeValue(world_, world_.component<Cells>(), BytesOf(original)).value();
  EXPECT_EQ(bytes.size(), sizeof(original.values));

  Cells restored;
  ASSERT_TRUE(ft::DecodeValue(world_, world_.component<Cells>(), MutableBytesOf(restored), bytes));
  EXPECT_EQ(restored.values, original.values);
}

TEST_F(ComponentCodecTest, AgreesWithTheJsonPath) {
  Everything original = Sample();
  original.precise = -2.5;  // flecs JSON writes tiny doubles like -2.5e-300 as -0

  const flecs::entity_t type = world_.component<Everything>();
  const std::string json = world_.to_json(type, &original).c_str();

  EXPECT_EQ(ft::ValueFromJson(world_, type, json).value(), Encode(original));
  EXPECT_EQ(ft::ValueToJson(world_, type, Encode(original)).value(), json);
}

TEST_F(ComponentCodecTest, RejectsTruncatedAndTrailingBytes) {
  const flecs::entity_t type = world_.component<Everything>();
  std::vector<uint8_t> bytes = Encode(Sample());

  for (std::size_t size = 0; size < bytes.size(); ++size) {
    EXPECT_FALSE(ft::ValidateValue(world_, type, std::span(bytes).first(size))) << size;
  }
  bytes.push_back(0);
  EXPECT_FALSE(ft::ValidateValue(world_, type, bytes));
}

TEST_F(ComponentCodecTest, RejectsCorruptCounts) {
  const flecs::entity_t matrix = world_.component<Eigen::Matrix4f>();
  const Eigen::Matrix4f identity = Eigen::Matrix4f::Identity();
  std::vector<uint8_t> bytes = ft::EncodeValue(world_, matrix, BytesOf(identity)).value();
  ASSERT_TRUE(ft::ValidateValue(world_, matrix, bytes));

  // Padded, so only the count is wrong.
  const uint32_t too_many = 17;
  std::memcpy(bytes.data() + kMatrixCountOffset, &too_many, sizeof(too_many));
  bytes.resize(bytes.size() + sizeof(float));
  EXPECT_FALSE(ft::ValidateValue(world_, matrix, bytes));

  const flecs::entity_t string = world_.component<std::string>();
  const std::vector<uint8_t> huge_string = {0xFF, 0xFF, 0xFF, 0xFF, 'x'};
  EXPECT_FALSE(ft::ValidateValue(world_, string, huge_string));
}

TEST_F(ComponentCodecTest, EntitiesTravelByPath) {
  const flecs::entity_t type = world_.component<Link>();
  const Link original {.target = target_.id()};
  const auto bytes = ft::EncodeValue(world_, type, BytesOf(original)).value();

  Link restored;
  ASSERT_TRUE(ft::DecodeValue(world_, type, MutableBytesOf(restored), bytes));
  EXPECT_EQ(restored.target, original.target);

  target_.destruct();
  EXPECT_FALSE(ft::ValidateValue(world_, type, bytes));
}

template <class T>
std::vector<uint8_t> RawBytes(T value) {
  std::vector<uint8_t> bytes(sizeof(T));
  std::memcpy(bytes.data(), &value, sizeof(T));
  return bytes;
}

TEST_F(ComponentCodecTest, RejectsAnUnknownEnumConstant) {
  const flecs::entity_t type = world_.component<Mood>();

  EXPECT_TRUE(ft::ValidateValue(world_, type, RawBytes(Mood::kAngry)));
  EXPECT_FALSE(ft::ValidateValue(world_, type, RawBytes(kUnknownMood)));
}

TEST_F(ComponentCodecTest, RejectsUnknownBitmaskFlags) {
  const flecs::entity_t type = world_.component<Flags>();

  EXPECT_TRUE(ft::ValidateValue(world_, type, RawBytes(kFlagA | kFlagB)));
  EXPECT_FALSE(ft::ValidateValue(world_, type, RawBytes(kFlagA | kUnknownFlag)));
}

TEST_F(ComponentCodecTest, GarbageNeverDecodes) {
  const flecs::entity_t type = world_.component<Everything>();
  const std::vector<uint8_t> garbage(Encode(Sample()).size(), 0xFF);

  EXPECT_FALSE(ft::ValidateValue(world_, type, garbage));
}

}  // namespace
