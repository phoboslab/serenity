/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
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

#include <LibWeb/DOM/Element.h>
#include <LibWeb/Layout/LayoutTableCell.h>
#include <LibWeb/Layout/LayoutTableRow.h>

namespace Web {

LayoutTableRow::LayoutTableRow(const Element& element, NonnullRefPtr<StyleProperties> style)
    : LayoutBox(&element, move(style))
{
}

LayoutTableRow::~LayoutTableRow()
{
}

void LayoutTableRow::layout(LayoutMode layout_mode)
{
    LayoutBox::layout(layout_mode);
}

LayoutTableCell* LayoutTableRow::first_cell()
{
    return first_child_of_type<LayoutTableCell>();
}

const LayoutTableCell* LayoutTableRow::first_cell() const
{
    return first_child_of_type<LayoutTableCell>();
}

LayoutTableRow* LayoutTableRow::next_row()
{
    return next_sibling_of_type<LayoutTableRow>();
}

const LayoutTableRow* LayoutTableRow::next_row() const
{
    return next_sibling_of_type<LayoutTableRow>();
}

}
