#include <cstdint>
#include <rstd/test/gtest.hpp>

#include "shaders/hlsl.inc"
#include "shaders/layout.inc"
#include "shaders/numeric.inc"
#include "shaders/packing.inc"
#include "shaders/resources.inc"
import rstd;
import spirvto;

TEST(Reflection, RejectsInvalidHeader) {
  rstd::uint32_t words[] = {0, 0x10000, 0, 1, 0};
  auto result = spirvto::reflect(
      rstd::slice<rstd::uint32_t>::from_raw_parts(words, rstd::usize(5)));
  ASSERT_TRUE(result.is_err());
  EXPECT_EQ(result.unwrap_err().kind, spirvto::ErrorKind::InvalidHeader);
}

using namespace rstd::prelude;
using namespace rstd::literals;

template <rstd::size_t N> auto reflect_words(const unsigned (&words)[N]) {
  return spirvto::reflect(slice<unsigned>::from_raw_parts(words, usize(N)));
}

TEST(Reflection, ReflectsOwnedLayoutAndInterface) {
  auto result = reflect_words(layout_words);
  ASSERT_TRUE(result.is_ok())
      << static_cast<unsigned>(result.unwrap_err().kind);
  auto r = result.unwrap();
  EXPECT_EQ(r.entry_point.as_str(), "main"_str);
  EXPECT_EQ(r.execution_model, spirvto::spv::ExecutionModel::Vertex);
  ASSERT_EQ(r.bindings.len(), usize(1));
  const auto &b = r.bindings[usize(0)];
  EXPECT_EQ(b.name.as_str(), "params"_str);
  EXPECT_EQ(b.type_name.as_str(), "Params"_str);
  EXPECT_EQ(b.set, 1u);
  EXPECT_EQ(b.binding, 2u);
  EXPECT_TRUE(b.accessed);
  EXPECT_EQ(b.type, spirvto::DescriptorType::UniformBuffer);
  ASSERT_EQ(b.block.members.len(), usize(3));
  EXPECT_EQ(b.block.size, 116u);
  const auto &matrix = b.block.members[usize(0)];
  EXPECT_EQ(matrix.offset, 0u);
  EXPECT_EQ(matrix.size, 40u);
  EXPECT_EQ(matrix.matrix_major, spirvto::MatrixMajor::Row);
  EXPECT_EQ(matrix.matrix_stride, 16u);
  EXPECT_EQ(matrix.numeric.matrix_rows, 3u);
  EXPECT_EQ(matrix.numeric.matrix_columns, 2u);
  const auto &array = b.block.members[usize(1)];
  EXPECT_EQ(array.offset, 48u);
  EXPECT_EQ(array.size, 36u);
  EXPECT_EQ(array.array_stride, 16u);
  ASSERT_EQ(array.array_dimensions.len(), usize(1));
  EXPECT_EQ(array.array_dimensions[usize(0)], 3u);
  const auto &nested = b.block.members[usize(2)];
  EXPECT_EQ(nested.offset, 96u);
  EXPECT_EQ(nested.size, 20u);
  ASSERT_EQ(nested.members.len(), usize(2));
  EXPECT_EQ(nested.members[usize(1)].name.as_str(), "gain"_str);
  EXPECT_EQ(nested.members[usize(1)].offset, 16u);
  ASSERT_EQ(r.inputs.len(), usize(2));
  EXPECT_EQ(r.inputs[usize(0)].location, 1u);
  EXPECT_EQ(r.inputs[usize(0)].numeric.scalar_kind,
            spirvto::ScalarKind::UnsignedInteger);
}

TEST(Reflection, TracksResourcesThroughCalls) {
  auto result = reflect_words(resources_words);
  ASSERT_TRUE(result.is_ok())
      << static_cast<unsigned>(result.unwrap_err().kind);
  auto r = result.unwrap();
  EXPECT_EQ(r.execution_model, spirvto::spv::ExecutionModel::Fragment);
  ASSERT_EQ(r.bindings.len(), usize(2));
  for (const auto &b : r.bindings) {
    EXPECT_EQ(b.type, spirvto::DescriptorType::CombinedImageSampler);
    EXPECT_EQ(b.accessed, b.binding == 1);
  }
  ASSERT_EQ(r.push_constants.len(), usize(1));
  EXPECT_EQ(r.push_constants[usize(0)].size, 16u);
  ASSERT_EQ(r.outputs.len(), usize(1));
  EXPECT_EQ(r.outputs[usize(0)].location, 0u);
}

TEST(Reflection, PreservesExplicitPackingAndEveryArrayStride) {
  auto result = reflect_words(packing_words);
  ASSERT_TRUE(result.is_ok());
  const auto &block = result->bindings[usize(0)].block;
  EXPECT_EQ(block.size, 96u);
  ASSERT_EQ(block.members.len(), usize(6));
  EXPECT_EQ(block.members[usize(0)].size, 12u);
  EXPECT_EQ(block.members[usize(1)].offset, 12u);
  EXPECT_EQ(block.members[usize(2)].size, 4u);
  EXPECT_EQ(block.members[usize(3)].offset, 20u);
  const auto &matrices = block.members[usize(4)];
  EXPECT_EQ(matrices.size, 48u);
  EXPECT_EQ(matrices.matrix_stride, 8u);
  EXPECT_EQ(matrices.array_stride, 24u);
  EXPECT_EQ(matrices.matrix_major, spirvto::MatrixMajor::Row);
  const auto &grid = block.members[usize(5)];
  EXPECT_EQ(grid.size, 24u);
  ASSERT_EQ(grid.array_dimensions.len(), usize(2));
  ASSERT_EQ(grid.array_strides.len(), usize(2));
  EXPECT_EQ(grid.array_dimensions[usize(0)], 2u);
  EXPECT_EQ(grid.array_dimensions[usize(1)], 3u);
  EXPECT_EQ(grid.array_strides[usize(0)], 12u);
  EXPECT_EQ(grid.array_strides[usize(1)], 4u);
}

TEST(Reflection, RejectsInvalidMatrixStrideAndOverflow) {
  const unsigned strides[] = {1u, 0xfffffff0u};
  for (unsigned stride : strides) {
    Vec<unsigned> code;
    for (auto word : layout_words)
      code.push(unsigned(word));
    for (usize p(5); p < code.len();) {
      auto count = code[p] >> 16;
      if (static_cast<spirvto::spv::Op>(code[p] & 0xffff) ==
              spirvto::spv::Op::MemberDecorate &&
          code[p + usize(3)] ==
              static_cast<unsigned>(spirvto::spv::Decoration::MatrixStride)) {
        code[p + usize(4)] = stride;
        break;
      }
      p += usize(count);
    }
    auto result = spirvto::reflect(code.as_slice());
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind, stride == 1
                                            ? spirvto::ErrorKind::InvalidOperand
                                            : spirvto::ErrorKind::Overflow);
    EXPECT_NE(result.unwrap_err().word_offset, 0u);
  }
}

TEST(Reflection, RejectsMissingLayoutDecorations) {
  Vec<unsigned> code;
  for (auto word : layout_words)
    code.push(unsigned(word));
  for (usize p(5); p < code.len();) {
    auto count = code[p] >> 16;
    if (static_cast<spirvto::spv::Op>(code[p] & 0xffff) ==
            spirvto::spv::Op::MemberDecorate &&
        code[p + usize(3)] ==
            static_cast<unsigned>(spirvto::spv::Decoration::MatrixStride)) {
      for (unsigned i = 0; i < count; ++i)
        code[p + usize(i)] = 1u << 16;
      break;
    }
    p += usize(count);
  }
  auto result = spirvto::reflect(code.as_slice());
  ASSERT_TRUE(result.is_err());
  EXPECT_EQ(result.unwrap_err().kind, spirvto::ErrorKind::MissingDecoration);
  EXPECT_NE(result.unwrap_err().word_offset, 0u);
}

TEST(Reflection, ReflectsWideScalarsAndColumnMajorMatrices) {
  auto result = reflect_words(numeric_words);
  ASSERT_TRUE(result.is_ok());
  const auto &block = result->bindings[usize(0)].block;
  ASSERT_EQ(block.members.len(), usize(5));
  EXPECT_EQ(block.size, 96u);
  EXPECT_EQ(block.members[usize(0)].numeric.scalar_width, 64u);
  EXPECT_EQ(block.members[usize(0)].size, 8u);
  EXPECT_EQ(block.members[usize(1)].offset, 32u);
  EXPECT_EQ(block.members[usize(1)].size, 24u);
  EXPECT_EQ(block.members[usize(2)].numeric.scalar_kind,
            spirvto::ScalarKind::SignedInteger);
  EXPECT_EQ(block.members[usize(3)].numeric.scalar_kind,
            spirvto::ScalarKind::UnsignedInteger);
  const auto &matrix = block.members[usize(4)];
  EXPECT_EQ(matrix.matrix_major, spirvto::MatrixMajor::Column);
  EXPECT_EQ(matrix.matrix_stride, 8u);
  EXPECT_EQ(matrix.numeric.matrix_rows, 2u);
  EXPECT_EQ(matrix.numeric.matrix_columns, 3u);
  EXPECT_EQ(matrix.size, 24u);
}

TEST(Reflection, TracksHlslImageAndSamplerThroughParameters) {
  auto result = reflect_words(hlsl_words);
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(result->execution_model, spirvto::spv::ExecutionModel::Fragment);
  EXPECT_EQ(result->entry_point.as_str(), "main"_str);
  ASSERT_EQ(result->bindings.len(), usize(3));
  unsigned images = 0, samplers = 0, blocks = 0;
  for (const auto &binding : result->bindings) {
    EXPECT_TRUE(binding.accessed);
    EXPECT_EQ(binding.set, 0u);
    if (binding.type == spirvto::DescriptorType::UniformBuffer) {
      ++blocks;
      EXPECT_EQ(binding.type_name.as_str(), "Params"_str);
      EXPECT_EQ(binding.block.size, 52u);
      ASSERT_EQ(binding.block.members.len(), usize(2));
      const auto &matrix = binding.block.members[usize(0)];
      EXPECT_EQ(matrix.matrix_major, spirvto::MatrixMajor::Column);
      EXPECT_EQ(matrix.matrix_stride, 16u);
      EXPECT_EQ(matrix.numeric.matrix_rows, 3u);
      EXPECT_EQ(matrix.numeric.matrix_columns, 2u);
      EXPECT_EQ(matrix.size, 28u);
      EXPECT_EQ(binding.block.members[usize(1)].offset, 32u);
    } else {
      EXPECT_EQ(binding.binding, 1u);
      images += binding.type == spirvto::DescriptorType::SampledImage;
      samplers += binding.type == spirvto::DescriptorType::Sampler;
    }
  }
  EXPECT_EQ(blocks, 1u);
  EXPECT_EQ(images, 1u);
  EXPECT_EQ(samplers, 1u);
}

TEST(Reflection, RejectsTruncatedInstructionsAndUnknownEntry) {
  auto words = slice<unsigned>::from_raw_parts(layout_words,
                                               usize(sizeof(layout_words) / 4));
  auto truncated = spirvto::reflect(
      slice<unsigned>::from_raw_parts(layout_words, words.len() - usize(1)));
  ASSERT_TRUE(truncated.is_err());
  auto missing = spirvto::reflect(words, "missing"_str);
  ASSERT_TRUE(missing.is_err());
  EXPECT_EQ(missing.unwrap_err().kind, spirvto::ErrorKind::MissingEntryPoint);
}

TEST(Reflection, SelectsEntryAndDoesNotTreatLiteralAsResourceId) {
  using spirvto::spv::Op;
  constexpr auto op = [](Op opcode, unsigned words) {
    return (words << 16) | static_cast<unsigned>(opcode);
  };
  const unsigned words[] = {
      0x07230203,
      0x10000,
      0,
      15,
      0,
      op(Op::Capability, 2),
      1,
      op(Op::MemoryModel, 3),
      0,
      1,
      op(Op::EntryPoint, 5),
      4,
      10,
      0x73726966,
      0x74,
      op(Op::EntryPoint, 5),
      4,
      11,
      0x6568746f,
      0x72,
      op(Op::ExecutionMode, 3),
      10,
      7,
      op(Op::ExecutionMode, 3),
      11,
      7,
      op(Op::String, 4),
      6,
      0x72756f73,
      0x6563,
      op(Op::Decorate, 4),
      9,
      34,
      0,
      op(Op::Decorate, 4),
      9,
      33,
      1,
      op(Op::TypeVoid, 2),
      1,
      op(Op::TypeFunction, 3),
      2,
      1,
      op(Op::TypeFloat, 3),
      3,
      32,
      op(Op::TypeImage, 9),
      4,
      3,
      1,
      0,
      0,
      0,
      1,
      0,
      op(Op::TypePointer, 4),
      5,
      0,
      4,
      op(Op::Variable, 4),
      5,
      9,
      0,
      op(Op::Function, 5),
      1,
      10,
      0,
      2,
      op(Op::Label, 2),
      12,
      op(Op::Load, 4),
      4,
      13,
      9,
      op(Op::Return, 1),
      op(Op::FunctionEnd, 1),
      op(Op::Function, 5),
      1,
      11,
      0,
      2,
      op(Op::Label, 2),
      14,
      op(Op::Line, 4),
      6,
      9,
      9,
      op(Op::Return, 1),
      op(Op::FunctionEnd, 1),
  };
  auto code = slice<unsigned>::from_raw_parts(words, usize(sizeof(words) / 4));
  auto first = spirvto::reflect(code);
  ASSERT_TRUE(first.is_ok());
  EXPECT_EQ(first->entry_point.as_str(), "first"_str);
  EXPECT_TRUE(first->bindings[usize(0)].accessed);
  auto other = spirvto::reflect(code, "other"_str);
  ASSERT_TRUE(other.is_ok());
  EXPECT_EQ(other->entry_point.as_str(), "other"_str);
  EXPECT_FALSE(other->bindings[usize(0)].accessed);
}

TEST(Reflection, OwnsNamesAfterInputIsReleased) {
  Vec<unsigned> code;
  for (auto word : layout_words)
    code.push(unsigned(word));
  auto reflected = spirvto::reflect(code.as_slice());
  ASSERT_TRUE(reflected.is_ok());
  code.clear();
  EXPECT_EQ(reflected->entry_point.as_str(), "main"_str);
  EXPECT_EQ(reflected->bindings[usize(0)].block.members[usize(0)].name.as_str(),
            "transform"_str);
}

TEST(Reflection, RejectsMissingIdsAndMalformedStrings) {
  Vec<unsigned> code;
  for (auto word : layout_words)
    code.push(unsigned(word));
  // Make the missing reference fit the declared bound, without defining it.
  code[usize(3)] += 1;
  for (usize p(5); p < code.len();) {
    unsigned count = code[p] >> 16;
    if (static_cast<spirvto::spv::Op>(code[p] & 0xffff) ==
        spirvto::spv::Op::Load) {
      code[p + usize(3)] = code[usize(3)] - 1;
      break;
    }
    p += usize(count);
  }
  auto missing = spirvto::reflect(code.as_slice());
  ASSERT_TRUE(missing.is_err());
  EXPECT_EQ(missing.unwrap_err().kind, spirvto::ErrorKind::InvalidId);
  unsigned string_words[] = {0x07230203, 0x10000,        0, 2,
                             0,          (3u << 16) | 7, 1, 0xffffffff};
  auto invalid = reflect_words(string_words);
  ASSERT_TRUE(invalid.is_err());
  EXPECT_EQ(invalid.unwrap_err().kind, spirvto::ErrorKind::InvalidString);
}
