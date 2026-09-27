export module spirvto:reflect;

import rstd;
export import :spirv;

using namespace rstd::prelude;

export namespace spirvto {
using Word = rstd::uint32_t;

enum class ErrorKind {
  InvalidHeader,
  TruncatedInstruction,
  UnknownInstruction,
  InvalidOperand,
  InvalidId,
  InvalidString,
  MissingEntryPoint,
  UnsupportedType,
  UnsupportedConstant,
  UnsupportedDecoration,
  MissingDecoration,
  RecursiveType,
  Overflow
};
struct Error {
  ErrorKind kind;
  Word word_offset;
};
enum class ScalarKind {
  Unknown,
  Boolean,
  SignedInteger,
  UnsignedInteger,
  Float
};
enum class MatrixMajor { None, Column, Row };
enum class DescriptorType {
  Sampler,
  CombinedImageSampler,
  SampledImage,
  StorageImage,
  UniformTexelBuffer,
  StorageTexelBuffer,
  UniformBuffer,
  StorageBuffer,
  InputAttachment,
  AccelerationStructure
};
struct NumericType {
  ScalarKind scalar_kind = ScalarKind::Unknown;
  Word scalar_width = 0;
  Word vector_components = 1;
  Word matrix_rows = 0;
  Word matrix_columns = 0;
};
struct BlockVariable {
  String name;
  // Offset is relative to the parent; size excludes trailing padding.
  Word offset = 0;
  Word size = 0;
  NumericType numeric;
  MatrixMajor matrix_major = MatrixMajor::None;
  Word matrix_stride = 0;
  Word array_stride = 0;
  Vec<Word> array_dimensions;
  Vec<Word> array_strides;
  Vec<BlockVariable> members;
};
struct DescriptorBinding {
  String name;
  String type_name;
  Word id = 0;
  Word set = 0;
  Word binding = 0;
  Word count = 1;
  DescriptorType type = DescriptorType::Sampler;
  bool accessed = false;
  BlockVariable block;
};
struct InterfaceVariable {
  String name;
  Word location = 0xffffffffu;
  Word component = 0;
  bool built_in = false;
  NumericType numeric;
  Vec<Word> array_dimensions;
  Vec<InterfaceVariable> members;
};
struct Reflection {
  String entry_point;
  spv::ExecutionModel execution_model = spv::ExecutionModel::Vertex;
  Vec<DescriptorBinding> bindings;
  Vec<BlockVariable> push_constants;
  Vec<InterfaceVariable> inputs;
  Vec<InterfaceVariable> outputs;
};
auto reflect(slice<Word> code, ref<str> entry_point)
    -> Result<Reflection, Error>;
auto reflect(slice<Word> code) -> Result<Reflection, Error>;
} // namespace spirvto
