# Text encodings

The **Encoding** menu separates two operations:

- **Reload with Encoding** reads the file's bytes using the chosen character set. Unsaved edits require an explicit discard confirmation. A decoding/read failure leaves the current buffer untouched.
- **Convert to Encoding** selects the format for the next Save, preserves the editor text and marks the tab dirty. Save preflights every character and commits atomically; unrepresentable characters cause an error before the existing file is touched. Choose UTF-8/UTF-16 to preserve them. Text undo does not change the selected save encoding; select the previous format in the menu.

The status bar shows the selected format. Supported codecs include UTF-8 with/without BOM, UTF-16 LE/BE with/without BOM, Windows-1252, Latin-1/Latin-9, Shift-JIS, EUC-JP, GB18030/GBK, Big5, EUC-KR, Windows-1251 and KOI8-R when available in Qt. Editor buffers and plugin text coordinates remain Unicode/UTF-8/UTF-16 regardless of the file format.

Opening honors a BOM and valid UTF-8 first. For other files, a charset-detector guess is accepted only when decoding and re-encoding reproduce the original bytes. An inconclusive result uses a reversible Latin-1 mapping; detection does not prove the intended language. Reload explicitly when the displayed text is wrong. Session recovery stores the buffer as UTF-8 plus its encoding preference, including pending text the chosen legacy format cannot save.

**Edit → Encoding/Decoding** contains Base64 and URL operations on selected text. These are different from file character-set conversion. Native SDK metadata reports the selected encoding and BOM; encoding conversion is currently an editor UI operation.
