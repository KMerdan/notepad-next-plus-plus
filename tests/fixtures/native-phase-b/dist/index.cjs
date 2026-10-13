// SPDX-License-Identifier: GPL-3.0-or-later
const assert=require('node:assert/strict');
exports.activate=async context=>{
  context.subscriptions.push(await context.commands.register('test.native-b.run',async invocation=>{
    assert.equal(context.host.apiVersion,'0.3.0');const id=invocation.documentId,editorId=invocation.editorId;
    const limits=await context.host.getNativeLimits();assert.equal(limits.maxRangeUtf8Bytes,262144);
    const events=[],watch=await context.documents.watchNative(event=>events.push(event));
    const selections=await context.editors.watchSelections(editorId,event=>events.push(event));assert.equal(selections.selection.documentId,id);
    const info=await context.documents.getInfo(id),range={start:{line:0,character:0},end:{line:0,character:8}};
    assert.equal((await context.documents.readRange(id,info.version,range)).text,'TODO 😀中');
    const options=await context.editors.getOptions(editorId);assert.ok(options.tabWidth>0);
    await context.editors.revealRange(editorId,info.version,range);await context.editors.activate(editorId,info.version,{preserveFocus:true});
    const query=await context.search.findAll(id,info.version,{text:'TODO',wholeWord:true,maxResults:1});assert.equal(query.count,1);assert.equal(query.complete,false);
    const next=await context.search.count(id,info.version,{text:'TODO',wholeWord:true,cursor:query.next});assert.equal(next.count,1);
    const languages=await context.languages.list();assert.ok(languages.some(value=>value.id==='text'));await context.languages.getKeywords('text');
    await context.languages.set(id,info.version,'text');
    await context.bookmarks.set(id,info.version,[0],true);assert.ok((await context.bookmarks.list(id)).lines.includes(0));
    const decoration=await context.decorations.create(id,{style:'highlight',color:'#007aff'});await decoration.set(info.version,[range]);await decoration.dispose();
    const selected=await context.editors.getSelections(editorId);
    await context.core.execute('edit.lines.duplicate',{target:{editorId,documentId:id,expectedVersion:info.version,selectionRevision:selected.revision}});
    const edited=await context.documents.getInfo(id);assert.ok(edited.version>info.version);
    await context.documents.convertLineEndings(id,edited.version,'lf');
    const created=await context.files.create({text:'Generated 😀中\n',languageId:'text',activate:false});assert.equal(created.status,'completed');
    const saved=await context.files.saveAs(created.document.documentId,created.document.version,invocation.args[0]);assert.equal(saved.status,'completed');
    assert.equal((await context.files.save(saved.document.documentId,saved.document.version)).status,'skipped');
    const opened=await context.files.open(invocation.args[0],{activate:false});assert.equal(opened.document.documentId,saved.document.documentId);
    assert.equal((await context.files.close(saved.document.documentId,saved.document.version,'ifClean')).status,'completed');
    const original=await context.documents.getInfo(id);assert.equal((await context.files.close(id,original.version,'ifClean')).status,'skipped');
    await watch.dispose();await selections.dispose();
    return {apiVersion:context.host.apiVersion,limits,queryCount:query.count+next.count,events:events.map(value=>value.type)};
  }));
};
