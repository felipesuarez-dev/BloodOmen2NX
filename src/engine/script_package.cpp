#include "bo2/engine/script_package.hpp"

#include "bo2/common/reader.hpp"

namespace bo2::engine {

namespace {

// Sequential reader with the alignment of the package writer: u8 is not
// aligned, u16 is aligned to 2 and u32 to 4, counted from the start of the
// payload (FUN_0069d310, FUN_0069d3a0, FUN_0069d3e0).
class Cursor {
public:
    Cursor(const std::vector<std::uint8_t>& data, const std::vector<std::uint16_t>& symbols,
           const std::vector<std::string>& names)
        : reader_(data), symbols_(symbols), names_(names) {}

    std::size_t position() const { return position_; }
    std::size_t size() const { return reader_.size(); }
    void seek(std::size_t position) { position_ = position; }

    std::uint8_t u8() {
        const std::uint8_t value = reader_.u8(position_);
        position_ += 1;
        return value;
    }

    std::uint16_t u16() {
        align(2);
        const std::uint16_t value = reader_.u16(position_);
        position_ += 2;
        return value;
    }

    std::uint32_t u32() {
        align(4);
        const std::uint32_t value = reader_.u32(position_);
        position_ += 4;
        return value;
    }

    // A name reference (FUN_0058cf00): a u16 index into the entry's symbols.
    std::string name() {
        const std::uint16_t index = u16();
        if (index >= symbols_.size() || symbols_[index] >= names_.size()) {
            throw common::ParseError("script package name index is out of range");
        }
        return names_[symbols_[index]];
    }

    // A chunk (FUN_0069c8f0): a u32 size, then that many bytes. Returns the
    // end of the chunk.
    std::size_t chunk() {
        const std::uint32_t length = u32();
        if (length > reader_.size() - position_) {
            throw common::ParseError("script package chunk ends past the data");
        }
        return position_ + length;
    }

    // The content of a chunk may end up to three bytes before the chunk.
    void close(std::size_t end) {
        if (position_ > end || end - position_ >= 4) {
            throw common::ParseError("script package chunk content does not fill its chunk");
        }
        position_ = end;
    }

    std::vector<std::uint8_t> bytes(std::size_t offset, std::size_t length) const {
        return reader_.bytes(offset, length);
    }

private:
    void align(std::size_t step) { position_ = (position_ + step - 1) & ~(step - 1); }

    common::Reader reader_;
    const std::vector<std::uint16_t>& symbols_;
    const std::vector<std::string>& names_;
    std::size_t position_ = 0;
};

// FUN_0058ce80: a u16 kind, then the element type and a u32 count (6), the
// wrapped type (7) or a name (9, 10, 11).
ScriptType read_type(Cursor& in, int depth = 0) {
    if (depth > 16) {
        throw common::ParseError("script type nests too deep");
    }
    ScriptType type;
    type.kind = in.u16();
    if (type.kind == 6) {
        type.inner.push_back(read_type(in, depth + 1));
        type.count = in.u32();
    } else if (type.kind == 7) {
        type.inner.push_back(read_type(in, depth + 1));
    } else if (type.kind == 9 || type.kind == 10 || type.kind == 11) {
        type.name = in.name();
    }
    return type;
}

// FUN_0058cf20: the default value of a field, by the kind of its type.
void read_default(Cursor& in, ScriptField& field) {
    switch (field.type.kind) {
    case 1:
    case 2:
    case 3:
    case 5:
    case 11:
        field.default_flag = in.u16();
        field.default_value = in.u32();
        break;
    case 4:
        field.default_flag = in.u16();
        if (field.default_flag != 0) {
            field.default_name = in.name();
        }
        break;
    case 6:
    case 7:
    case 10:
        field.default_flag = in.u16();
        break;
    case 9:
        field.default_flag = in.u16();
        if (field.default_flag != 0) {
            const std::size_t end = in.chunk();
            field.default_object = in.bytes(in.position(), end - in.position());
            in.seek(end);
        }
        break;
    default:
        break;
    }
}

std::vector<ScriptVariable> read_variables(Cursor& in, std::uint16_t count) {
    std::vector<ScriptVariable> variables;
    variables.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        ScriptVariable variable;
        variable.type = read_type(in);
        variable.offset = in.u32();
        variables.push_back(std::move(variable));
    }
    return variables;
}

// FUN_0058d000: the declaration of a class.
ScriptClass read_declaration(Cursor& in) {
    const std::size_t end = in.chunk();
    ScriptClass cls;
    cls.name = in.name();
    cls.kind = in.u16();
    cls.data_size = in.u16();
    cls.slot_count = in.u16();
    const std::uint16_t state_count = in.u16();
    const std::uint16_t enum_count = in.u16();
    const std::uint16_t field_count = in.u16();
    const std::uint16_t method_count = in.u16();
    const std::uint16_t body_count = in.u16();
    for (std::uint16_t index = 0; index < state_count; ++index) {
        cls.states.push_back(in.name());
    }
    for (std::uint16_t index = 0; index < enum_count; ++index) {
        ScriptEnum entry;
        entry.name = in.name();
        entry.word = in.u16();
        const std::uint16_t count = in.u16();
        std::vector<std::string> value_names;
        for (std::uint16_t value = 0; value < count; ++value) {
            value_names.push_back(in.name());
        }
        for (std::uint16_t value = 0; value < count; ++value) {
            entry.values.emplace_back(value_names[value], in.u32());
        }
        cls.enums.push_back(std::move(entry));
    }
    for (std::uint16_t index = 0; index < field_count; ++index) {
        ScriptField field;
        field.name = in.name();
        field.flags = in.u16();
        field.offset = in.u32();
        field.type = read_type(in);
        read_default(in, field);
        cls.fields.push_back(std::move(field));
    }
    for (std::uint16_t index = 0; index < method_count; ++index) {
        ScriptMethod method;
        method.name = in.name();
        method.byte4 = in.u8();
        method.byte5 = in.u8();
        method.slot = in.u16();
        if (method.slot != kScriptNoSlot && method.slot >= cls.slot_count) {
            throw common::ParseError("script method slot is out of range");
        }
        method.result = read_type(in);
        method.params = read_variables(in, in.u16());
        cls.methods.push_back(std::move(method));
    }
    for (std::uint16_t index = 0; index < body_count; ++index) {
        ScriptBody body;
        body.method = in.u16();
        body.flags = in.u16();
        body.state = in.u16();
        body.locals_size = in.u16();
        body.stack_size = in.u16();
        if (body.method >= cls.methods.size()) {
            throw common::ParseError("script body names a method that does not exist");
        }
        if (body.state >= cls.states.size()) {
            throw common::ParseError("script body names a state that does not exist");
        }
        body.locals = read_variables(in, in.u16());
        if (!body.native()) {
            const std::uint32_t words = in.u32();
            if (words > (in.size() - in.position()) / 4) {
                throw common::ParseError("script body code ends past the data");
            }
            body.code.reserve(words);
            for (std::uint32_t word = 0; word < words; ++word) {
                body.code.push_back(in.u32());
            }
        }
        cls.bodies.push_back(std::move(body));
    }
    in.close(end);
    cls.references.resize(cls.bodies.size());
    return cls;
}

// FUN_0058d420: the link record of a class.
void read_link(Cursor& in, ScriptClass& cls) {
    const std::size_t end = in.chunk();
    if (in.u16() != 0) {
        cls.parent = in.name();
    }
    for (std::size_t state = 0; state < cls.states.size(); ++state) {
        cls.state_owners.push_back(in.u16() != 0 ? in.name() : std::string());
    }
    for (std::uint16_t state = in.u16(); state != 0xFFFF; state = in.u16()) {
        ScriptSlot slot;
        slot.state = state;
        slot.slot = in.u16();
        slot.class_name = in.name();
        slot.body = in.u16();
        if (slot.state >= cls.states.size() || slot.slot >= cls.slot_count) {
            throw common::ParseError("script slot entry is out of range");
        }
        cls.slots.push_back(std::move(slot));
    }
    for (std::uint16_t body = in.u16(); body != 0xFFFF; body = in.u16()) {
        if (body >= cls.bodies.size()) {
            throw common::ParseError("script references name a body that does not exist");
        }
        const std::size_t words = cls.bodies[body].code.size();
        for (std::uint16_t kind = in.u16(); kind != 0xFFFF; kind = in.u16()) {
            ScriptReference reference;
            reference.kind = kind;
            reference.word = in.u16();
            switch (kind) {
            case 0:
            case 1:
            case 2:
                reference.name = in.name();
                break;
            case 3:
            case 4:
                reference.name = in.name();
                reference.index = in.u16();
                break;
            case 5:
                reference.name = in.name();
                reference.second = in.name();
                reference.index = in.u16();
                break;
            default:
                throw common::ParseError("script reference kind is unknown");
            }
            const std::size_t needed = kind == 5 ? 2 : 1;
            if (reference.word + needed > words) {
                throw common::ParseError("script reference points past the code of its body");
            }
            cls.references[body].push_back(std::move(reference));
        }
    }
    in.close(end);
}

}  // namespace

// FUN_0058cbb0: version, imports, the declarations, the link records and a
// final 0.
ScriptPackage parse_script_package(const std::vector<std::uint8_t>& data,
                                   const std::vector<std::uint16_t>& symbols,
                                   const std::vector<std::string>& names) {
    Cursor in(data, symbols, names);
    ScriptPackage package;
    package.version = in.u16();
    if (package.version != 1) {
        throw common::ParseError("unsupported script package version");
    }
    const std::uint16_t import_count = in.u16();
    for (std::uint16_t index = 0; index < import_count; ++index) {
        package.imports.push_back(in.name());
    }
    const std::uint16_t class_count = in.u16();
    // A declaration takes at least one byte: a count above what remains is a
    // truncated package and must fail before the vector reserves memory.
    if (class_count > in.size() - in.position()) {
        throw common::ParseError("script package class count overruns the data");
    }
    package.classes.reserve(class_count);
    for (std::uint16_t index = 0; index < class_count; ++index) {
        package.classes.push_back(read_declaration(in));
    }
    for (ScriptClass& cls : package.classes) {
        read_link(in, cls);
    }
    if (in.u32() != 0 || in.position() != data.size()) {
        throw common::ParseError("script package has trailing data");
    }
    return package;
}

std::size_t script_instruction_words(std::uint32_t word) {
    switch (word >> 24) {
    case 0x49:
    case 0x4F:
    case 0x55:
        return 3;
    case 0x02:
    case 0x21:
    case 0x26:
    case 0x47:
    case 0x48:
    case 0x4A:
    case 0x4B:
    case 0x4C:
    case 0x4D:
    case 0x4E:
    case 0x50:
    case 0x51:
    case 0x52:
    case 0x53:
    case 0x54:
    case 0x56:
    case 0x57:
    case 0x58:
        return 2;
    default:
        return 1;
    }
}

}  // namespace bo2::engine
