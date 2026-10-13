// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScintillaEdit.h"

namespace SearchService {
// One bounded native literal search. Finder retains its target; plugin tasks
// restore it immediately, before yielding the GUI thread to user interaction.
inline Sci_CharacterRangeFull findLiteral(ScintillaEdit *editor, const QByteArray &text, int flags,
                                      sptr_t start, sptr_t end, bool preserveState) {
    const auto oldFlags=editor->searchFlags(), oldStart=editor->targetStart(), oldEnd=editor->targetEnd();
    const auto oldStartVirtual=editor->targetStartVirtualSpace(), oldEndVirtual=editor->targetEndVirtualSpace();
    editor->setSearchFlags(flags); editor->setTargetRange(start,end);
    const auto found=editor->searchInTarget(text.size(),text.constData());
    const Sci_CharacterRangeFull result=found==INVALID_POSITION ? Sci_CharacterRangeFull{INVALID_POSITION,INVALID_POSITION}
        : Sci_CharacterRangeFull{editor->targetStart(),editor->targetEnd()};
    if(preserveState) {
        editor->setSearchFlags(oldFlags); editor->setTargetRange(oldStart,oldEnd);
        editor->setTargetStartVirtualSpace(oldStartVirtual); editor->setTargetEndVirtualSpace(oldEndVirtual);
    }
    return result;
}
}
