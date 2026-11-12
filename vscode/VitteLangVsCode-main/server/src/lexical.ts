/** Shared lexical boundaries. Offsets use UTF-16, as required by LSP. */
export interface LexicalSpan {
  start: number;
  end: number;
  kind: "comment" | "string";
  closed: boolean;
}

export function scanLexical(text: string): { mask: Uint8Array; spans: LexicalSpan[] } {
  const mask = new Uint8Array(text.length).fill(1);
  const spans: LexicalSpan[] = [];
  let i = 0;
  while (i < text.length) {
    const start = i;
    let kind: LexicalSpan["kind"] = "comment";
    let closed = false;
    if (text.startsWith("//", i) || (text[i] === "#" &&
        !text.startsWith("#[", i) && !text.startsWith("#![", i))) {
      while (i < text.length && text[i] !== "\n" && text[i] !== "\r") i++;
      // At a line-ending caret, completion is still inside the comment.
      closed = false;
    } else if (text.startsWith("/*", i)) {
      i += 2;
      let depth = 1;
      while (i < text.length && depth > 0) {
        if (text.startsWith("/*", i)) { depth++; i += 2; }
        else if (text.startsWith("*/", i)) { depth--; i += 2; }
        else i++;
      }
      closed = depth === 0;
    } else if (text.startsWith("<<<", i)) {
      const end = text.indexOf(">>>", i + 3);
      closed = end !== -1;
      i = closed ? end + 3 : text.length;
    } else {
      kind = "string";
      // A raw-string prefix must begin a token, not end an identifier.
      const raw = text[i] === "r" && (i === 0 || !/[\p{L}\p{N}_]/u.test(text[i - 1]))
        ? /^r(#*)"/.exec(text.slice(i)) : null;
      if (raw) {
        const terminator = '"' + raw[1];
        const end = text.indexOf(terminator, i + raw[0].length);
        closed = end !== -1;
        i = closed ? end + terminator.length : text.length;
      } else if (text[i] === '"' || text[i] === "'") {
        const quote = text.startsWith('"""', i) ? '"""' : text[i];
        i += quote.length;
        while (i < text.length) {
          if (text[i] === "\\") { i = Math.min(i + 2, text.length); continue; }
          if (text.startsWith(quote, i)) { i += quote.length; closed = true; break; }
          i++;
        }
      } else {
        i++;
        continue;
      }
    }
    mask.fill(0, start, i);
    spans.push({ start, end: i, kind, closed });
  }
  return { mask, spans };
}

/** Blank non-code without changing offsets, CRLF, or surrogate-pair widths. */
export function maskNonCode(text: string): string {
  const { mask } = scanLexical(text);
  return text.split("").map((ch, i) => mask[i] || ch === "\n" || ch === "\r" ? ch : " ").join("");
}

export function lexicalContextAt(text: string, offset: number): "code" | "comment" | "string" {
  const cursor = Math.max(0, Math.min(offset, text.length));
  for (const span of scanLexical(text).spans) {
    if (span.start < cursor && (cursor < span.end || (cursor === span.end && !span.closed))) {
      return span.kind;
    }
  }
  return "code";
}
