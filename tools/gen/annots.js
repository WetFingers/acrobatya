// annots.js — создаёт аннотации MuPDF по JSON-описанию (для make_annots.py и make_demo.py).
// Что: mutool run tools/gen/annots.js <вход.pdf> <spec.json> <выход.pdf>
// Откуда: spec.json пишет генератор на Python; координаты в пространстве PDF (y вверх),
// здесь переводятся в координаты MuPDF (y вниз от верха страницы).
// Детерминизм: даты создания/изменения ставятся из spec.fixed_ms; генератор после этого
// ещё раз нормализует /M и /CreationDate и пересохраняет файл через pikepdf.

var inPath = scriptArgs[0], specPath = scriptArgs[1], outPath = scriptArgs[2];
var spec = JSON.parse(read(specPath));
var doc = Document.openDocument(inPath).asPDF();
var fixed = spec.fixed_ms;

function flipRect(H, r) { return [r[0], H - r[3], r[2], H - r[1]]; }
function flipPt(H, p) { return [p[0], H - p[1]]; }
function quad(H, r) {
	// r = [x0, y0, x1, y1] в PDF; квад MuPDF: ul, ur, ll, lr
	var t = H - r[3], b = H - r[1];
	return [r[0], t, r[2], t, r[0], b, r[2], b];
}

var created = [];
for (var i = 0; i < spec.annots.length; i++) {
	var s = spec.annots[i];
	var page = doc.loadPage(s.page);
	var H = page.getBounds()[3];
	var a;
	if (s.type === "Link") {
		page.createLink(flipRect(H, s.rect), s.uri);
		created.push({ nm: s.nm, type: "Link" });
		continue;
	}
	a = page.createAnnotation(s.type);
	if (s.rect && a.hasRect()) a.setRect(flipRect(H, s.rect));
	if (s.color) a.setColor(s.color);
	if (s.interior) a.setInteriorColor(s.interior);
	if (s.border_width !== undefined) a.setBorderWidth(s.border_width);
	if (s.opacity !== undefined) a.setOpacity(s.opacity);
	if (s.icon) a.setIcon(s.icon);
	if (s.da) a.setDefaultAppearance(s.da[0], s.da[1], s.da[2]);
	if (s.quadding !== undefined) a.setQuadding(s.quadding);
	if (s.line) a.setLine(flipPt(H, s.line[0]), flipPt(H, s.line[1]));
	if (s.line_endings) a.setLineEndingStyles(s.line_endings[0], s.line_endings[1]);
	if (s.vertices) {
		var vs = [];
		for (var k = 0; k < s.vertices.length; k++) vs.push(flipPt(H, s.vertices[k]));
		a.setVertices(vs);
	}
	if (s.ink) {
		var strokes = [];
		for (var k = 0; k < s.ink.length; k++) {
			var st = [];
			for (var m = 0; m < s.ink[k].length; m++) st.push(flipPt(H, s.ink[k][m]));
			strokes.push(st);
		}
		a.setInkList(strokes);
	}
	if (s.quads) {
		var qs = [];
		for (var k = 0; k < s.quads.length; k++) qs.push(quad(H, s.quads[k]));
		a.setQuadPoints(qs);
	}
	if (s.attach) {
		var fs = doc.addEmbeddedFile(s.attach.name, s.attach.mime, s.attach.text, fixed, fixed, false);
		a.setFilespec(fs);
	}
	if (s.contents) a.setContents(s.contents);
	if (s.author) a.setAuthor(s.author);
	if (s.subject) a.setSubject(s.subject);
	if (s.popup) { a.setPopup(flipRect(H, s.popup)); }
	if (s.open !== undefined) a.setIsOpen(s.open);
	a.setCreationDate(fixed);
	a.setModificationDate(fixed);
	a.update();
	if (s.nm) a.getObject().put("NM", s.nm);
	a.setModificationDate(fixed);
	created.push({ nm: s.nm, type: s.type });
}
doc.save(outPath, "compress");
print(JSON.stringify({ created: created.length }));
