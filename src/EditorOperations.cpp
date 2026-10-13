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
    add("edit.lines.duplicate","Duplicate lines","Native current line or stream selections");
    add("edit.lines.move","Move lines","Native selected lines, up or down");
    add("edit.lines.join","Join lines","Native selection target");add("edit.lines.split","Split lines","Native selection target and editor width");
    add("edit.lines.reverse","Reverse lines","Selected complete lines, or whole document when selection is empty");
    add("edit.comment.add","Comment lines","Native selected lines");add("edit.comment.remove","Uncomment lines","Native selected lines");
    add("edit.indent.increase","Increase indent","Native selection indentation");add("edit.indent.decrease","Decrease indent","Native selection indentation");
    add("view.fold.level","Fold/unfold level","Requested native fold level 1-9",true,false);
    return result;
}
bool execute(ScintillaNext *editor,const QString &id,const QJsonObject &options) {
    if(!editor)return false;
    if(id=="edit.undo"){if(!editor->readOnly())editor->undo();return true;}
    if(id=="edit.redo"){if(!editor->readOnly())editor->redo();return true;}
    if(id=="view.fold.all"){editor->foldAll((options["folded"].toBool()?SC_FOLDACTION_CONTRACT:SC_FOLDACTION_EXPAND)|SC_FOLDACTION_CONTRACT_EVERY_LEVEL);return true;}
    if(id=="view.fold.level") {const auto level=options["level"].toInt();if(level<1||level>9)return false;if(options["folded"].toBool())editor->foldAllLevels(level-1);else editor->unFoldAllLevels(level-1);return true;}
    if(editor->readOnly())return false;
    const UndoAction transaction(editor);
    if(id=="edit.case.upper")editor->upperCase();
    else if(id=="edit.case.lower")editor->lowerCase();
    else if(id=="edit.comment.toggle")editor->toggleCommentSelection();
    else if(id=="edit.comment.add")editor->commentLineSelection();
    else if(id=="edit.comment.remove")editor->uncommentLineSelection();
    else if(id=="edit.lines.duplicate")editor->lineDuplicate();
    else if(id=="edit.lines.move") {const auto direction=options["direction"].toString();if(direction=="up")editor->moveSelectedLinesUp();else if(direction=="down")editor->moveSelectedLinesDown();else return false;}
    else if(id=="edit.lines.join"){editor->targetFromSelection();editor->linesJoin();}
    else if(id=="edit.lines.split"){editor->targetFromSelection();editor->linesSplit(0);}
    else if(id=="edit.lines.reverse"){ScintillaSorter sorter(editor);sorter.sort(ReverseSorter(Sorter::Direction::Ascending));}
    else if(id=="edit.indent.increase")editor->tab();
    else if(id=="edit.indent.decrease")editor->backTab();
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
