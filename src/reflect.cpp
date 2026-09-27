module spirvto;
import :grammar;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace spirvto;
using rstd::collections::BTreeMap;
using spv::Decoration;
using spv::Op;

struct Instruction {
  Word offset = 0, count = 0, result = 0, type = 0, function = 0;
  Op op = Op::Nop;
  Vec<Word> ids;
};
struct DecorationInfo {
  Word target, member, kind, value;
};
struct NameInfo {
  Word target, member;
  String value;
};

class Parser {
  slice<Word> code;
  Vec<Instruction> instructions;
  BTreeMap<u32, usize> definitions;
  Vec<DecorationInfo> decorations;
  Vec<NameInfo> names;
  Error error{ErrorKind::InvalidHeader, 0};
  bool failed = false;
  Word bound = 0;

  auto fail(ErrorKind kind, Word offset) -> bool {
    if (!failed)
      error = {kind, offset};
    failed = true;
    return false;
  }
  auto word(Word offset) const -> Word { return code[usize(offset)]; }
  auto get(Word id) -> const Instruction * {
    auto position = definitions.get(u32(id));
    if (position.is_none()) {
      fail(ErrorKind::InvalidId, 0);
      return nullptr;
    }
    return &instructions[**position];
  }
  auto value(const Instruction &i, Word index) -> Word {
    if (index >= i.count) {
      fail(ErrorKind::InvalidOperand, i.offset);
      return 0;
    }
    return word(i.offset + index);
  }
  auto string(Word &cursor, Word end) -> String {
    Vec<u8> bytes;
    bool terminated = false;
    while (cursor < end && !terminated) {
      Word part = word(cursor++);
      for (Word shift = 0; shift < 32; shift += 8) {
        auto byte = (part >> shift) & 255;
        if (byte == 0) {
          terminated = true;
          break;
        }
        bytes.push(u8(byte));
      }
    }
    if (!terminated) {
      fail(ErrorKind::InvalidString, cursor);
      return {};
    }
    auto text = String::from_utf8(rstd::move(bytes));
    if (text.is_err()) {
      fail(ErrorKind::InvalidString, cursor);
      return {};
    }
    return rstd::move(text).unwrap();
  }
  auto operands(Instruction &i, Word offset, Word count, Word &cursor, Word end,
                Word depth) -> bool {
    if (depth > 32)
      return fail(ErrorKind::InvalidOperand, cursor);
    for (Word p = 0; p < count; ++p) {
      auto operand = grammar::operands[offset + p];
      if (operand.quantifier && cursor == end)
        continue;
      do {
        if (!operand_value(i, operand.kind, cursor, end, depth + 1))
          return false;
      } while (operand.quantifier == 2 && cursor < end);
    }
    return true;
  }
  auto operand_value(Instruction &i, Word kind_index, Word &cursor, Word end,
                     Word depth) -> bool {
    const auto kind = grammar::kinds[kind_index];
    if (kind.category == 4)
      return operands(i, kind.base, kind.bases, cursor, end, depth);
    if (cursor >= end)
      return fail(ErrorKind::TruncatedInstruction, cursor);
    const Word v = word(cursor);
    if (kind.special == 1) {
      (void)string(cursor, end);
      return !failed;
    }
    if (kind.special == 2 ||
        (i.op == Op::Switch && kind.category == 1 && cursor >= i.offset + 3)) {
      // Constants and switch literals use the width of their declared type.
      Word width = 32;
      Word type = i.type;
      if (i.op == Op::Switch) {
        auto selector = get(value(i, 1));
        if (!selector)
          return false;
        type = selector->type;
      }
      if (type) {
        auto t = get(type);
        if (!t)
          return false;
        width = value(*t, 2);
      }
      const Word length = (width + 31) / 32;
      if (!length || length > end - cursor)
        return fail(ErrorKind::InvalidOperand, cursor);
      cursor += length;
      return true;
    }
    ++cursor;
    if (kind.category == 0) {
      if (!v || v >= bound)
        return fail(ErrorKind::InvalidId, cursor - 1);
      if (kind.special == 3)
        i.result = v;
      else {
        i.ids.push(Word(v));
        if (kind.special == 4)
          i.type = v;
      }
    } else if (kind.category == 2 || kind.category == 3) {
      Word remaining = v;
      bool matched = false;
      for (Word p = 0; p < kind.count; ++p) {
        const auto e = grammar::enumerants[kind.offset + p];
        const bool match = kind.category == 2
                               ? e.value == v
                               : (e.value ? (v & e.value) == e.value : v == 0);
        if (!match)
          continue;
        matched = true;
        remaining &= ~e.value;
        if (!operands(i, e.offset, e.count, cursor, end, depth))
          return false;
        if (kind.category == 2)
          break;
      }
      if (!matched || (kind.category == 3 && remaining))
        return fail(ErrorKind::InvalidOperand, cursor - 1);
    }
    return true;
  }
  auto decoration(Word id, Decoration kind, Word member = 0xffffffffu) const
      -> Option<Word> {
    for (const auto &d : decorations)
      if (d.target == id && d.member == member &&
          d.kind == static_cast<Word>(kind))
        return Some(Word(d.value));
    return None<Word>();
  }
  auto name(Word id, Word member = 0xffffffffu) const -> String {
    for (const auto &n : names)
      if (n.target == id && n.member == member)
        return n.value.clone();
    return {};
  }
  auto constant(Word id) -> Word {
    auto n = get(id);
    if (!n)
      return 0;
    if (n->op != Op::Constant) {
      fail(ErrorKind::UnsupportedConstant, n->offset);
      return 0;
    }
    auto type = get(n->type);
    if (!type || type->op != Op::TypeInt || value(*type, 2) != 32 ||
        n->count != 4) {
      fail(ErrorKind::UnsupportedConstant, n->offset);
      return 0;
    }
    return value(*n, 3);
  }
  auto multiply(Word a, Word b) -> Word {
    if (b && a > 0xffffffffu / b) {
      fail(ErrorKind::Overflow, 0);
      return 0;
    }
    return a * b;
  }
  auto add(Word a, Word b) -> Word {
    if (a > 0xffffffffu - b) {
      fail(ErrorKind::Overflow, 0);
      return 0;
    }
    return a + b;
  }
  auto numeric(Word id, Word depth = 0) -> NumericType {
    NumericType result;
    if (depth > 64) {
      fail(ErrorKind::RecursiveType, 0);
      return result;
    }
    auto n = get(id);
    if (!n)
      return result;
    switch (n->op) {
    case Op::TypeBool:
      result.scalar_kind = ScalarKind::Boolean;
      result.scalar_width = 32;
      break;
    case Op::TypeInt:
      result.scalar_kind = value(*n, 3) ? ScalarKind::SignedInteger
                                        : ScalarKind::UnsignedInteger;
      result.scalar_width = value(*n, 2);
      break;
    case Op::TypeFloat:
      result.scalar_kind = ScalarKind::Float;
      result.scalar_width = value(*n, 2);
      break;
    case Op::TypeVector:
      result = numeric(value(*n, 2), depth + 1);
      result.vector_components = value(*n, 3);
      break;
    case Op::TypeMatrix:
      result = numeric(value(*n, 2), depth + 1);
      result.matrix_rows = result.vector_components;
      result.matrix_columns = value(*n, 3);
      break;
    default:
      fail(ErrorKind::UnsupportedType, n->offset);
      break;
    }
    if (result.scalar_kind != ScalarKind::Boolean && result.scalar_width != 8 &&
        result.scalar_width != 16 && result.scalar_width != 32 &&
        result.scalar_width != 64)
      fail(ErrorKind::UnsupportedType, n->offset);
    if (!result.vector_components || result.vector_components > 4 ||
        result.matrix_columns > 4)
      fail(ErrorKind::UnsupportedType, n->offset);
    return result;
  }
  auto block(Word id, Word owner, Word member, Word depth = 0)
      -> BlockVariable {
    BlockVariable out;
    out.name = name(owner, member);
    if (depth > 64) {
      fail(ErrorKind::RecursiveType, 0);
      return out;
    }
    auto n = get(id);
    if (!n)
      return out;
    if (member != 0xffffffffu) {
      auto offset = decoration(owner, Decoration::Offset, member);
      if (offset.is_none()) {
        fail(ErrorKind::MissingDecoration, n->offset);
        return out;
      }
      out.offset = *offset;
    }
    if (n->op == Op::TypeArray) {
      out = block(value(*n, 2), owner, member, depth + 1);
      auto stride = decoration(id, Decoration::ArrayStride);
      if (stride.is_none()) {
        fail(ErrorKind::MissingDecoration, n->offset);
        return out;
      }
      auto length = constant(value(*n, 3));
      Vec<Word> dimensions;
      dimensions.push(Word(length));
      for (auto d : out.array_dimensions)
        dimensions.push(Word(d));
      out.array_dimensions = rstd::move(dimensions);
      out.array_stride = *stride;
      if (!length || *stride < out.size)
        fail(ErrorKind::InvalidOperand, n->offset);
      out.size = multiply(length, *stride);
    } else if (n->op == Op::TypeStruct) {
      Word extent = 0;
      for (Word j = 2; j < n->count; ++j) {
        auto child = block(value(*n, j), id, j - 2, depth + 1);
        auto end = add(child.offset, child.size);
        if (end > extent)
          extent = end;
        out.members.push(rstd::move(child));
      }
      for (auto &child : out.members) {
        for (const auto &next : out.members) {
          if (&child != &next && child.offset == next.offset)
            fail(ErrorKind::InvalidOperand, n->offset);
          if (next.offset > child.offset &&
              child.size > next.offset - child.offset)
            child.size = next.offset - child.offset;
        }
      }
      // Match the padded block extent consumed by SPIRV-Reflect clients.
      out.size = add(extent, 15) & ~Word(15);
    } else {
      out.numeric = numeric(id);
      if (out.numeric.matrix_columns) {
        auto stride = decoration(owner, Decoration::MatrixStride, member);
        const bool row =
            decoration(owner, Decoration::RowMajor, member).is_some();
        const bool column =
            decoration(owner, Decoration::ColMajor, member).is_some();
        if (stride.is_none() || row == column) {
          fail(ErrorKind::MissingDecoration, n->offset);
          return out;
        }
        out.matrix_major = row ? MatrixMajor::Row : MatrixMajor::Column;
        out.matrix_stride = *stride;
        out.size = multiply(*stride, row ? out.numeric.matrix_rows
                                         : out.numeric.matrix_columns);
      } else
        out.size = multiply(out.numeric.scalar_width / 8,
                            out.numeric.vector_components);
    }
    return out;
  }
  auto interface(Word id, Word owner, Word member = 0xffffffffu, Word depth = 0)
      -> InterfaceVariable {
    InterfaceVariable out;
    out.name = name(owner, member);
    out.location =
        decoration(owner, Decoration::Location, member).unwrap_or(0xffffffffu);
    out.component =
        decoration(owner, Decoration::Component, member).unwrap_or(0);
    out.built_in = decoration(owner, Decoration::BuiltIn, member).is_some();
    if (depth > 64) {
      fail(ErrorKind::RecursiveType, 0);
      return out;
    }
    auto n = get(id);
    if (!n)
      return out;
    if (n->op == Op::TypeArray) {
      out = interface(value(*n, 2), owner, member, depth + 1);
      Vec<Word> dims;
      dims.push(constant(value(*n, 3)));
      for (auto d : out.array_dimensions)
        dims.push(Word(d));
      out.array_dimensions = rstd::move(dims);
    } else if (n->op == Op::TypeStruct) {
      for (Word j = 2; j < n->count; ++j)
        out.members.push(interface(value(*n, j), id, j - 2, depth + 1));
    } else
      out.numeric = numeric(id);
    return out;
  }
  auto parse() -> bool {
    if (code.len() < usize(5) || code.len() > usize(0xffffffffu))
      return fail(ErrorKind::InvalidHeader, 0);
    bound = word(3);
    if (word(0) != spv::MagicNumber || !bound || word(4) ||
        (word(1) & 0xff0000ffu) || word(1) < 0x10000 || word(1) > spv::Version)
      return fail(ErrorKind::InvalidHeader, 0);
    Word function = 0;
    for (Word offset = 5; offset < code.len().to_primitive();) {
      Instruction i;
      i.offset = offset;
      i.count = word(offset) >> 16;
      i.op = static_cast<Op>(word(offset) & 0xffff);
      if (!i.count || i.count > code.len().to_primitive() - offset)
        return fail(ErrorKind::TruncatedInstruction, offset);
      const grammar::Instruction *schema = nullptr;
      Word low = 0,
           high = sizeof(grammar::instructions) / sizeof(grammar::Instruction);
      while (low < high) {
        Word mid = low + (high - low) / 2;
        if (grammar::instructions[mid].opcode < static_cast<Word>(i.op))
          low = mid + 1;
        else
          high = mid;
      }
      if (low < sizeof(grammar::instructions) / sizeof(grammar::Instruction) &&
          grammar::instructions[low].opcode == static_cast<Word>(i.op))
        schema = &grammar::instructions[low];
      if (!schema)
        return fail(ErrorKind::UnknownInstruction, offset);
      Word cursor = offset + 1;
      if (!operands(i, schema->offset, schema->count, cursor, offset + i.count,
                    0))
        return false;
      if (cursor != offset + i.count)
        return fail(ErrorKind::InvalidOperand, cursor);
      if (i.op == Op::Function)
        function = i.result;
      i.function = function;
      if (i.op == Op::FunctionEnd)
        function = 0;
      if (i.result) {
        if (definitions.get(u32(i.result)).is_some())
          return fail(ErrorKind::InvalidId, offset);
        (void)definitions.insert(u32(i.result), instructions.len());
      }
      if (i.op == Op::Name || i.op == Op::MemberName) {
        const Word member = i.op == Op::Name ? 0xffffffffu : value(i, 2);
        Word start = offset + (i.op == Op::Name ? 2 : 3);
        names.push(
            NameInfo{value(i, 1), member, string(start, offset + i.count)});
      }
      if (i.op == Op::Decorate || i.op == Op::MemberDecorate) {
        const Word index = i.op == Op::Decorate ? 2 : 3;
        decorations.push({value(i, 1), index == 2 ? 0xffffffffu : value(i, 2),
                          value(i, index),
                          i.count > index + 1 ? value(i, index + 1) : 0});
      }
      if (i.op == Op::GroupDecorate || i.op == Op::GroupMemberDecorate ||
          i.op == Op::DecorateId)
        return fail(ErrorKind::InvalidOperand, offset);
      offset += i.count;
      instructions.push(rstd::move(i));
    }
    if (function)
      return fail(ErrorKind::TruncatedInstruction, 0);
    for (const auto &i : instructions)
      for (auto id : i.ids)
        if (!get(id))
          return false;
    return !failed;
  }

public:
  explicit Parser(slice<Word> code) : code(code) {}
  auto run(ref<str> requested) -> Result<Reflection, Error> {
    if (!parse())
      return Err(error);
    Reflection out;
    const Instruction *entry = nullptr;
    Word interfaces = 0;
    for (const auto &i : instructions)
      if (i.op == Op::EntryPoint) {
        Word cursor = i.offset + 3;
        auto text = string(cursor, i.offset + i.count);
        if (requested.is_empty() || text.as_str() == requested) {
          entry = &i;
          interfaces = cursor;
          out.entry_point = rstd::move(text);
          break;
        }
      }
    if (!entry)
      return Err(Error{ErrorKind::MissingEntryPoint, 0});
    out.execution_model = static_cast<spv::ExecutionModel>(value(*entry, 1));
    Vec<Word> pending;
    BTreeMap<u32, bool> used;
    pending.push(value(*entry, 2));
    for (usize p{}; p < pending.len(); ++p) {
      Word id = pending[p];
      if (used.get(u32(id)).is_some())
        continue;
      (void)used.insert(u32(id), true);
      auto n = get(id);
      if (!n)
        return Err(error);
      for (auto ref : n->ids)
        pending.push(Word(ref));
      if (n->op == Op::Function)
        for (const auto &i : instructions)
          if (i.function == id)
            for (auto ref : i.ids)
              pending.push(Word(ref));
    }
    for (Word p = interfaces; p < entry->offset + entry->count; ++p) {
      auto n = get(word(p));
      if (!n || n->op != Op::Variable)
        return Err(Error{ErrorKind::InvalidId, p});
      const auto storage = static_cast<spv::StorageClass>(value(*n, 3));
      if (storage != spv::StorageClass::Input &&
          storage != spv::StorageClass::Output)
        continue;
      auto type = get(n->type);
      if (!type || type->op != Op::TypePointer)
        return Err(Error{ErrorKind::UnsupportedType, p});
      auto variable = interface(value(*type, 3), n->result);
      if (storage == spv::StorageClass::Input)
        out.inputs.push(rstd::move(variable));
      else
        out.outputs.push(rstd::move(variable));
    }
    for (const auto &n : instructions)
      if (n.op == Op::Variable && !n.function) {
        auto storage = static_cast<spv::StorageClass>(value(n, 3));
        if (storage != spv::StorageClass::UniformConstant &&
            storage != spv::StorageClass::Uniform &&
            storage != spv::StorageClass::StorageBuffer &&
            storage != spv::StorageClass::PushConstant)
          continue;
        auto pointer = get(n.type);
        if (!pointer || pointer->op != Op::TypePointer)
          return Err(Error{ErrorKind::UnsupportedType, n.offset});
        Word id = value(*pointer, 3);
        if (storage == spv::StorageClass::PushConstant) {
          if (used.get(u32(n.result)).is_some())
            out.push_constants.push(block(id, n.result, 0xffffffffu));
          continue;
        }
        DescriptorBinding b;
        b.id = n.result;
        b.name = name(n.result);
        b.accessed = used.get(u32(n.result)).is_some();
        auto set = decoration(n.result, Decoration::DescriptorSet);
        auto binding = decoration(n.result, Decoration::Binding);
        if (set.is_none() || binding.is_none())
          return Err(Error{ErrorKind::MissingDecoration, n.offset});
        b.set = *set;
        b.binding = *binding;
        auto t = get(id);
        Word array_depth = 0;
        while (t && t->op == Op::TypeArray && array_depth++ < 64) {
          b.count = multiply(b.count, constant(value(*t, 3)));
          id = value(*t, 2);
          t = get(id);
        }
        if (!t)
          return Err(error);
        b.type_name = name(id);
        switch (t->op) {
        case Op::TypeSampler:
          b.type = DescriptorType::Sampler;
          break;
        case Op::TypeSampledImage:
          b.type = DescriptorType::CombinedImageSampler;
          break;
        case Op::TypeImage: {
          auto dim = static_cast<spv::Dim>(value(*t, 3));
          Word sampled = value(*t, 7);
          if (dim == spv::Dim::SubpassData)
            b.type = DescriptorType::InputAttachment;
          else if (sampled != 1 && sampled != 2)
            return Err(Error{ErrorKind::UnsupportedType, t->offset});
          else if (dim == spv::Dim::Buffer)
            b.type = sampled == 1 ? DescriptorType::UniformTexelBuffer
                                  : DescriptorType::StorageTexelBuffer;
          else
            b.type = sampled == 1 ? DescriptorType::SampledImage
                                  : DescriptorType::StorageImage;
          break;
        }
        case Op::TypeStruct:
          if (storage == spv::StorageClass::StorageBuffer ||
              decoration(id, Decoration::BufferBlock).is_some())
            b.type = DescriptorType::StorageBuffer;
          else if (storage == spv::StorageClass::Uniform &&
                   decoration(id, Decoration::Block).is_some())
            b.type = DescriptorType::UniformBuffer;
          else
            return Err(Error{ErrorKind::UnsupportedType, t->offset});
          b.block = block(id, n.result, 0xffffffffu);
          break;
        case Op::TypeAccelerationStructureKHR:
          b.type = DescriptorType::AccelerationStructure;
          break;
        default:
          return Err(Error{ErrorKind::UnsupportedType, t->offset});
        }
        out.bindings.push(rstd::move(b));
      }
    if (failed)
      return Err(error);
    return Ok(rstd::move(out));
  }
};

auto spirvto::reflect(slice<Word> code, ref<str> entry_point)
    -> Result<Reflection, Error> {
  return Parser(code).run(entry_point);
}
auto spirvto::reflect(slice<Word> code) -> Result<Reflection, Error> {
  return reflect(code, ""_str);
}
