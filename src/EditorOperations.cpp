// SPDX-License-Identifier: GPL-3.0-or-later
#include "EditorOperations.h"
#include "ScintillaNext.h"
#include "ScintillaSorter.h"
#include "UndoAction.h"
namespace EditorOperations {
QJsonArray catalog() {
    QJsonArray result;
    const auto add=[&result](const char *id,const char *title,const char *selection,bool view=false,bool undo=true){
        result.append(QJsonObject{{"id",id},{"title",title},{"effect",view?"view":"document"},{"selectionSemantics",selection},{"createsUndoStep",undo},
            {"requiredCapabilities",QJsonArray{"core.commands@1",view?"editors.view.write@1":"documents.write@1"}}});
    };
    add("edit.undo","Undo","Existing undo stack",false,false);add("edit.redo","Redo","Existing redo stack",false,false);
    add("edit.case.upper","Uppercase","Native stream selections");add("edit.case.lower","Lowercase","Native stream selections");
    add("edit.comment.toggle","Toggle comment","Native selected lines");add("edit.lines.deduplicate","Remove duplicate lines","Whole document, preserves native line-ending semantics");
    add("edit.lines.sort","Sort lines","Selected complete lines, or whole document when selection is empty");
    add("view.fold.all","Fold/unfold all","All fold levels in the target editor",true,false);
    return result;
}
bool execute(ScintillaNext *editor,const QString &id,const QJsonObject &options) {
    if(!editor)return false;
    if(id=="edit.undo"){if(!editor->readOnly())editor->undo();return true;}
    if(id=="edit.redo"){if(!editor->readOnly())editor->redo();return true;}
    if(id=="view.fold.all"){editor->foldAll((options["folded"].toBool()?SC_FOLDACTION_CONTRACT:SC_FOLDACTION_EXPAND)|SC_FOLDACTION_CONTRACT_EVERY_LEVEL);return true;}
    if(editor->readOnly())return false;
    if(id=="edit.case.upper")editor->upperCase();
    else if(id=="edit.case.lower")editor->lowerCase();
    else if(id=="edit.comment.toggle")editor->toggleCommentSelection();
    else if(id=="edit.lines.deduplicate"){if(options["consecutiveOnly"].toBool())editor->removeConsecutiveDuplicateLines();else editor->removeDuplicateLines();}
    else if(id=="edit.lines.sort"){
        const auto order=options["order"].toString();const auto compare=options["compare"].toString();
        if((order!="ascending"&&order!="descending")||(compare!="caseSensitive"&&compare!="caseInsensitive"&&compare!="length"))return false;
        const auto direction=order=="ascending"?Sorter::Direction::Ascending:Sorter::Direction::Descending;
        const UndoAction undo(editor);ScintillaSorter sorter(editor);
        if(compare=="caseInsensitive")sorter.sort(CaseInsensitiveSorter(direction));else if(compare=="length")sorter.sort(LineLengthSorter(direction));else sorter.sort(CaseSensitiveSorter(direction));
    }else return false;
    return true;
}
}
