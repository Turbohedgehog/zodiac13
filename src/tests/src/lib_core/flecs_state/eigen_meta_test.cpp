#include <gtest/gtest.h>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/component_meta.h>

namespace {

namespace ft = z13::flecs_tools;

Eigen::Matrix4f SampleMatrix() {
  Eigen::Matrix4f m = Eigen::Matrix4f::Identity();
  m(0, 3) = 1.f / 3.f;
  m(1, 3) = -2.5f;
  m(2, 3) = 12345.678f;
  m(0, 1) = 0.1f;
  return m;
}

TEST(EigenMeta, MatrixRoundTripsThroughFlecsJson) {
  flecs::world world;
  ft::RegisterEigenMeta(world);
  const flecs::id_t id = world.component<Eigen::Matrix4f>();
  const Eigen::Matrix4f original = SampleMatrix();

  const flecs::string json = world.to_json(id, &original);
  ASSERT_GT(json.size(), 0u);

  Eigen::Matrix4f restored = Eigen::Matrix4f::Zero();
  ASSERT_NE(world.from_json(id, &restored, json.c_str()), nullptr);

  EXPECT_EQ(original, restored);
}

TEST(EigenMeta, MatrixIsSerializedAsSixteenNumbers) {
  flecs::world world;
  ft::RegisterEigenMeta(world);
  const Eigen::Matrix4f identity = Eigen::Matrix4f::Identity();

  const flecs::string json = world.to_json(world.component<Eigen::Matrix4f>(), &identity);

  EXPECT_EQ(std::string(json.c_str()), "[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]");
}

}  // namespace
