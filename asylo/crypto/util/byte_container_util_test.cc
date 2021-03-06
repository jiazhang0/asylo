/*
 *
 * Copyright 2017 Asylo authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "asylo/crypto/util/byte_container_util.h"

#include <cstdint>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "asylo/crypto/util/byte_container_view.h"
#include "asylo/test/util/status_matchers.h"
#include "asylo/util/cleansing_types.h"

namespace asylo {
namespace {

constexpr char kStr1[] = "foo";
constexpr char kStr2[] = "bar";
constexpr char kStr3[] = "baz";
constexpr char kStr4[] = "foobar";

// A test fixture is required for defining typed tests.
template <typename T>
class ByteContainerUtilTypedTest : public ::testing::Test {};

// Returns |value| as a 32-bit little-endian encoded integer. |value| must not
// exceed the max value of a uint32_t.
uint32_t EncodeLittleEndian(size_t value) {
#ifdef __x86_64__
  return value;
#else
#error "Only supported on x86_64 architecture"
#endif
}

// Types for the serialized output.
typedef ::testing::Types<std::string, std::vector<uint8_t>, std::string,
                         CleansingString, CleansingVector<uint8_t>>
    OutputTypes;

TYPED_TEST_CASE(ByteContainerUtilTypedTest, OutputTypes);

// Verify that the serialization of no strings is an empty string.
TYPED_TEST(ByteContainerUtilTypedTest, EmptySerialization) {
  TypeParam output(kStr4, kStr4 + sizeof(kStr4) - 1);
  EXPECT_THAT(SerializeByteContainers(&output), IsOk());
  EXPECT_EQ(0, output.size());
}

// Verify that appending the serialization of no strings to a non-empty string
// does not alter the existing string.
TYPED_TEST(ByteContainerUtilTypedTest, EmptySerializationAppend) {
  TypeParam output(kStr4, kStr4 + sizeof(kStr4) - 1);
  EXPECT_THAT(AppendSerializedByteContainers(&output), IsOk());
  EXPECT_EQ(ByteContainerView(kStr4), (output));
}

// Verify that a serialization contains all input strings and that the input
// strings can be inferred from the serialization.
TYPED_TEST(ByteContainerUtilTypedTest, SerializationContainsAllByteContainers) {
  TypeParam output1;
  EXPECT_THAT(SerializeByteContainers(&output1, kStr1, kStr2, kStr3), IsOk());

  std::vector<ByteContainerView> inputs = {kStr1, kStr2, kStr3};
  int index = 0;
  for (const auto &input : inputs) {
    uint32_t size = EncodeLittleEndian(input.size());
    ASSERT_EQ(0, memcmp(output1.data() + index, &size, sizeof(size)));
    index += sizeof(size);
    EXPECT_EQ(0, memcmp(output1.data() + index, input.data(), size));
    index += size;
  }

  TypeParam output2;
  for (const auto &input : inputs) {
    EXPECT_THAT(AppendSerializedByteContainers(&output2, input), IsOk());
  }

  // serialized([a, b, c]) == (serialized(a) || serialized(b) || serialized(c))
  EXPECT_EQ(ByteContainerView(output1), ByteContainerView(output2));
}

// Verify the serialization for a large number of inputs to exercise the
// recursive-template implementation.
TYPED_TEST(ByteContainerUtilTypedTest, SerializeLargeNumberOfInputs) {
  TypeParam output1;
  EXPECT_THAT(SerializeByteContainers(&output1, kStr1, kStr2, kStr3, kStr4,
                                      kStr1, kStr2, kStr3, kStr4, kStr1, kStr2,
                                      kStr3, kStr4, kStr1, kStr2, kStr3, kStr4),
              IsOk());

  std::vector<ByteContainerView> inputs = {
      kStr1, kStr2, kStr3, kStr4, kStr1, kStr2, kStr3, kStr4,
      kStr1, kStr2, kStr3, kStr4, kStr1, kStr2, kStr3, kStr4};

  TypeParam output2;
  for (const auto &input : inputs) {
    EXPECT_THAT(AppendSerializedByteContainers(&output2, input), IsOk());
  }

  // serialized([a, b, c]) == (serialized(a) || serialized(b) || serialized(c))
  EXPECT_EQ(ByteContainerView(output1), ByteContainerView(output2));
}

// Verify that serializations are unambiguous, and unique per set of input
// strings.
TYPED_TEST(ByteContainerUtilTypedTest, SerializationsAreUnique) {
  TypeParam output1;
  TypeParam output2;

  // Serialize(a, b, c)
  EXPECT_THAT(SerializeByteContainers(&output1, kStr1, kStr2, kStr3), IsOk());

  // Serialize(a || b, c)
  EXPECT_THAT(SerializeByteContainers(&output2, kStr4, kStr3), IsOk());

  // serialized(a, b, c) != serialized(a || b, c)
  EXPECT_NE(ByteContainerView(output1), ByteContainerView(output2));
}

// Verify that CopyToByteContainer correctly copies the contents.
TYPED_TEST(ByteContainerUtilTypedTest, CopyToByteContainer) {
  TypeParam container = CopyToByteContainer<TypeParam>(kStr1);
  EXPECT_EQ(ByteContainerView(container), ByteContainerView(kStr1));
}

TYPED_TEST(ByteContainerUtilTypedTest, SafeComparePositive) {
  TypeParam container(kStr1, kStr1 + sizeof(kStr1) - 1);
  EXPECT_TRUE(SafeCompareByteContainers(container, kStr1));
}

TYPED_TEST(ByteContainerUtilTypedTest, SafeCompareNegative) {
  TypeParam container(kStr1, kStr1 + sizeof(kStr1) - 1);
  EXPECT_FALSE(SafeCompareByteContainers(container, kStr2));
}

TEST(ByteContainerUtilTest, MakeStringView) {
  absl::string_view view1 = MakeView<absl::string_view>(kStr1);
  absl::string_view view2(kStr1);

  EXPECT_EQ(view1.data(), view2.data());
  EXPECT_EQ(view1.size(), view2.size());
}

TEST(ByteContainerUtilTest, MakeSpan) {
  std::vector<uint8_t> data =
      CopyToByteContainer<std::vector<uint8_t>>(kStr1);
  auto span1 = MakeView<absl::Span<const uint8_t>>(data);
  absl::Span<const uint8_t> span2(data);

  EXPECT_EQ(span1.data(), span2.data());
  EXPECT_EQ(span1.size(), span2.size());
}

}  // namespace
}  // namespace asylo
