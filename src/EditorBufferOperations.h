// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScintillaEdit.h"
namespace EditorBufferOperations {
inline void convertLineEndings(ScintillaEdit *editor,int mode) {
    editor->beginUndoAction();editor->convertEOLs(mode);editor->setEOLMode(mode);editor->endUndoAction();
}
}
