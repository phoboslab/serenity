/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2020, Linus Groh <mail@linusgroh.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <AK/Function.h>
#include <AK/StringBuilder.h>
#include <LibJS/Heap/Heap.h>
#include <LibJS/Interpreter.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/StringObject.h>
#include <LibJS/Runtime/StringPrototype.h>
#include <LibJS/Runtime/Value.h>
#include <string.h>

namespace JS {

static StringObject* string_object_from(Interpreter& interpreter)
{
    auto* this_object = interpreter.this_value().to_object(interpreter);
    if (!this_object)
        return nullptr;
    if (!this_object->is_string_object()) {
        interpreter.throw_exception<TypeError>("Not a String object");
        return nullptr;
    }
    return static_cast<StringObject*>(this_object);
}

static String string_from(Interpreter& interpreter)
{
    auto* this_object = interpreter.this_value().to_object(interpreter);
    if (!this_object)
        return {};
    return Value(this_object).to_string(interpreter);
}

StringPrototype::StringPrototype()
    : StringObject(*js_string(interpreter(), String::empty()), *interpreter().global_object().object_prototype())
{
    u8 attr = Attribute::Writable | Attribute::Configurable;

    define_native_property("length", length_getter, nullptr, 0);
    define_native_function("charAt", char_at, 1, attr);
    define_native_function("repeat", repeat, 1, attr);
    define_native_function("startsWith", starts_with, 1, attr);
    define_native_function("indexOf", index_of, 1, attr);
    define_native_function("toLowerCase", to_lowercase, 0, attr);
    define_native_function("toUpperCase", to_uppercase, 0, attr);
    define_native_function("toString", to_string, 0, attr);
    define_native_function("padStart", pad_start, 1, attr);
    define_native_function("padEnd", pad_end, 1, attr);
    define_native_function("trim", trim, 0, attr);
    define_native_function("trimStart", trim_start, 0, attr);
    define_native_function("trimEnd", trim_end, 0, attr);
    define_native_function("concat", concat, 1, attr);
    define_native_function("substring", substring, 2, attr);
    define_native_function("includes", includes, 1, attr);
    define_native_function("slice", slice, 2, attr);
    define_native_function("lastIndexOf", last_index_of, 1, attr);
}

StringPrototype::~StringPrototype()
{
}

Value StringPrototype::char_at(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    i32 index = 0;
    if (interpreter.argument_count()) {
        index = interpreter.argument(0).to_i32(interpreter);
        if (interpreter.exception())
            return {};
    }
    if (index < 0 || index >= static_cast<i32>(string.length()))
        return js_string(interpreter, String::empty());
    return js_string(interpreter, string.substring(index, 1));
}

Value StringPrototype::repeat(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    if (!interpreter.argument_count())
        return js_string(interpreter, String::empty());
    auto count_value = interpreter.argument(0).to_number(interpreter);
    if (interpreter.exception())
        return {};
    if (count_value.as_double() < 0)
        return interpreter.throw_exception<RangeError>("repeat count must be a positive number");
    if (count_value.is_infinity())
        return interpreter.throw_exception<RangeError>("repeat count must be a finite number");
    auto count = count_value.to_size_t(interpreter);
    if (interpreter.exception())
        return {};
    StringBuilder builder;
    for (size_t i = 0; i < count; ++i)
        builder.append(string);
    return js_string(interpreter, builder.to_string());
}

Value StringPrototype::starts_with(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    if (!interpreter.argument_count())
        return Value(false);
    auto search_string = interpreter.argument(0).to_string(interpreter);
    if (interpreter.exception())
        return {};
    auto string_length = string.length();
    auto search_string_length = search_string.length();
    size_t start = 0;
    if (interpreter.argument_count() > 1) {
        auto number = interpreter.argument(1).to_number(interpreter);
        if (interpreter.exception())
            return {};
        if (!number.is_nan())
            start = min(number.to_size_t(interpreter), string_length);
    }
    if (start + search_string_length > string_length)
        return Value(false);
    if (search_string_length == 0)
        return Value(true);
    return Value(string.substring(start, search_string_length) == search_string);
}

Value StringPrototype::index_of(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    auto needle = interpreter.argument(0).to_string(interpreter);
    if (interpreter.exception())
        return {};
    return Value((i32)string.index_of(needle).value_or(-1));
}

Value StringPrototype::to_lowercase(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    return js_string(interpreter, string.to_lowercase());
}

Value StringPrototype::to_uppercase(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    return js_string(interpreter, string.to_uppercase());
}

Value StringPrototype::length_getter(Interpreter& interpreter)
{
    auto* string_object = string_object_from(interpreter);
    if (!string_object)
        return {};
    return Value((i32)string_object->primitive_string().string().length());
}

Value StringPrototype::to_string(Interpreter& interpreter)
{
    auto* string_object = string_object_from(interpreter);
    if (!string_object)
        return {};
    return js_string(interpreter, string_object->primitive_string().string());
}

enum class PadPlacement {
    Start,
    End,
};

static Value pad_string(Interpreter& interpreter, const String& string, PadPlacement placement)
{
    auto max_length = interpreter.argument(0).to_size_t(interpreter);
    if (interpreter.exception())
        return {};
    if (max_length <= string.length())
        return js_string(interpreter, string);

    String fill_string = " ";
    if (!interpreter.argument(1).is_undefined()) {
        fill_string = interpreter.argument(1).to_string(interpreter);
        if (interpreter.exception())
            return {};
        if (fill_string.is_empty())
            return js_string(interpreter, string);
    }

    auto fill_length = max_length - string.length();

    StringBuilder filler_builder;
    while (filler_builder.length() < fill_length)
        filler_builder.append(fill_string);
    auto filler = filler_builder.build().substring(0, fill_length);

    if (placement == PadPlacement::Start)
        return js_string(interpreter, String::format("%s%s", filler.characters(), string.characters()));
    return js_string(interpreter, String::format("%s%s", string.characters(), filler.characters()));
}

Value StringPrototype::pad_start(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    return pad_string(interpreter, string, PadPlacement::Start);
}

Value StringPrototype::pad_end(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    return pad_string(interpreter, string, PadPlacement::End);
}

Value StringPrototype::trim(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    return js_string(interpreter, string.trim_whitespace(String::TrimMode::Both));
}

Value StringPrototype::trim_start(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    return js_string(interpreter, string.trim_whitespace(String::TrimMode::Left));
}

Value StringPrototype::trim_end(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    return js_string(interpreter, string.trim_whitespace(String::TrimMode::Right));
}

Value StringPrototype::concat(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    StringBuilder builder;
    builder.append(string);
    for (size_t i = 0; i < interpreter.argument_count(); ++i) {
        auto string_argument = interpreter.argument(i).to_string(interpreter);
        if (interpreter.exception())
            return {};
        builder.append(string_argument);
    }
    return js_string(interpreter, builder.to_string());
}

Value StringPrototype::substring(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    if (interpreter.argument_count() == 0)
        return js_string(interpreter, string);

    auto string_length = string.length();
    auto index_start = min(interpreter.argument(0).to_size_t(interpreter), string_length);
    if (interpreter.exception())
        return {};
    auto index_end = string_length;
    if (interpreter.argument_count() >= 2) {
        index_end = min(interpreter.argument(1).to_size_t(interpreter), string_length);
        if (interpreter.exception())
            return {};
    }

    if (index_start == index_end)
        return js_string(interpreter, String(""));

    if (index_start > index_end) {
        if (interpreter.argument_count() == 1)
            return js_string(interpreter, String(""));
        auto temp_index_start = index_start;
        index_start = index_end;
        index_end = temp_index_start;
    }

    auto part_length = index_end - index_start;
    auto string_part = string.substring(index_start, part_length);
    return js_string(interpreter, string_part);
}

Value StringPrototype::includes(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};
    auto search_string = interpreter.argument(0).to_string(interpreter);
    if (interpreter.exception())
        return {};

    size_t position = 0;
    if (interpreter.argument_count() >= 2) {
        position = interpreter.argument(1).to_size_t(interpreter);
        if (interpreter.exception())
            return {};
        if (position >= string.length())
            return Value(false);
    }

    if (position == 0)
        return Value(string.contains(search_string));

    auto substring_length = string.length() - position;
    auto substring_search = string.substring(position, substring_length);
    return Value(substring_search.contains(search_string));
}

Value StringPrototype::slice(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};

    if (interpreter.argument_count() == 0)
        return js_string(interpreter, string);

    auto string_length = static_cast<i32>(string.length());
    auto index_start = interpreter.argument(0).to_i32(interpreter);
    if (interpreter.exception())
        return {};
    auto index_end = string_length;

    auto negative_min_index = -(string_length - 1);
    if (index_start < negative_min_index)
        index_start = 0;
    else if (index_start < 0)
        index_start = string_length + index_start;

    if (interpreter.argument_count() >= 2) {
        index_end = interpreter.argument(1).to_i32(interpreter);
        if (interpreter.exception())
            return {};

        if (index_end < negative_min_index)
            return js_string(interpreter, String::empty());

        if (index_end > string_length)
            index_end = string_length;
        else if (index_end < 0)
            index_end = string_length + index_end;
    }

    if (index_start >= index_end)
        return js_string(interpreter, String::empty());

    auto part_length = index_end - index_start;
    auto string_part = string.substring(index_start, part_length);
    return js_string(interpreter, string_part);
}

Value StringPrototype::last_index_of(Interpreter& interpreter)
{
    auto string = string_from(interpreter);
    if (string.is_null())
        return {};

    if (interpreter.argument_count() == 0)
        return Value(-1);

    auto search_string = interpreter.argument(0).to_string(interpreter);
    if (interpreter.exception())
        return {};
    if (search_string.length() > string.length())
        return Value(-1);

    auto max_index = string.length() - search_string.length();
    auto from_index = max_index;
    if (interpreter.argument_count() >= 2) {
        from_index = min(interpreter.argument(1).to_size_t(interpreter), max_index);
        if (interpreter.exception())
            return {};
    }

    for (i32 i = from_index; i >= 0; --i) {
        auto part_view = string.substring_view(i, search_string.length());
        if (part_view == search_string)
            return Value(i);
    }

    return Value(-1);
}

}
