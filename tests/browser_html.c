/* Native HTML/CSS regression: compiles the real browser HTML parser and CSS
 * engine and asserts tokenizer/DOM/cascade behaviour on malformed and modern
 * markup. No network, raster or JS is involved. */
#include "../kernel/browser/browser.h"
extern int printf(const char *, ...);
extern void *malloc(__SIZE_TYPE__);
extern void free(void *);
extern void exit(int);
volatile u32 ticks;
BrowserApp g_browser;
static int failures;
static int has_sub(const char *hay, const char *needle) {
    for (const char *h = hay; *h; h++) {
        int k = 0; while (needle[k] && h[k] == needle[k]) k++;
        if (!needle[k]) return 1;
    }
    return 0;
}
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)
void *kmalloc(u32 n) { return malloc(n); }
void kfree(void *p) { free(p); }
void *kcalloc(u32 n, u32 s) { void *p = malloc(n * s); if (p) memset(p, 0, n * s); return p; }
void serial(const char *s) { (void)s; }
void number(char *s, u32 n) { int k=0; char b[12]; do {b[k++]='0'+n%10;n/=10;}while(n);int i=0;while(k)s[i++]=b[--k];s[i]=0; }
void browser_work_checkpoint(void) {}
void browser_mark_dirty(void) { g_browser.layout_dirty = 1; }
int ui_is_dark(void) { return 0; }
int sys_text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }
void js_init(void) {}
void js_execute(const char *c, DomNode *d) { (void)c;(void)d; }
void js_dispatch_event(DomNode *n, const char *t) { (void)n;(void)t; }
void js_set_event_pos(int x, int y) { (void)x;(void)y; }
void js_set_event_key(int k) { (void)k; }
void js_service_tasks(void) {}
int js_has_pending_tasks(void) { return 0; }

static DomNode *first_tag(DomNode *n, const char *tag) {
    if (!n) return 0;
    if (n->type == NODE_ELEMENT) {
        int k = 0; while (tag[k] && n->tag[k] == tag[k]) k++;
        if (tag[k] == 0 && n->tag[k] == 0) return n;
    }
    for (DomNode *c = n->first_child; c; c = c->next_sibling) {
        DomNode *r = first_tag(c, tag);
        if (r) return r;
    }
    return 0;
}
static int count_tag(DomNode *n, const char *tag) {
    if (!n) return 0;
    int total = 0;
    if (n->type == NODE_ELEMENT) { int k=0; while (tag[k] && n->tag[k]==tag[k]) k++; if (tag[k]==0 && n->tag[k]==0) total++; }
    for (DomNode *c = n->first_child; c; c = c->next_sibling) total += count_tag(c, tag);
    return total;
}
static const char *attr(DomNode *n, const char *name) { return dom_get_attribute(n, name); }

static void test_entities(void) {
    const char *html = "<p>&amp;&lt;&gt;&quot;&apos;&nbsp;&#65;&#x42;&#8230;&#x1F600;&copy;&unknown;</p>";
    DomNode *d = html_parse(html, (int)strlen(html));
    DomNode *p = first_tag(d, "p");
    CHECK(p && p->first_child && p->first_child->text);
    const char *t = p->first_child->text;
    CHECK(t[0] == '&' && t[1] == '<' && t[2] == '>' && t[3] == '"' && t[4] == '\'');
    CHECK(t[5] == ' ');
    CHECK(t[6] == 'A' && t[7] == 'B');
    CHECK((u8)t[8] == 0xE2 && (u8)t[9] == 0x80 && (u8)t[10] == 0xA6);
    CHECK((u8)t[11] == 0xF0 && (u8)t[12] == 0x9F && (u8)t[13] == 0x98 && (u8)t[14] == 0x80);
    CHECK((u8)t[15] == '(');
    dom_free_tree(d);
}
static void test_trade_entity_and_viewport_units(void) {
    const char *html = "<p id=trade>&trade;</p><div id=box></div>";
    DomNode *d = html_parse(html, (int)strlen(html));
    DomNode *trade = dom_get_element_by_id(d, "trade");
    CHECK(trade && trade->first_child && has_sub(trade->first_child->text, "(TM)"));

    g_browser.w = 820;
    g_browser.h = 510;
    css_apply_styles(d, "#box { width: 50vw; height: 50vh; }");
    DomNode *box = dom_get_element_by_id(d, "box");
    CHECK(box && box->style.width == 400);
    CHECK(box && box->style.height == 200);
    dom_free_tree(d);
}
static void test_malformed(void) {
    const char *html = "<div><p>one<p>two</div><b>bold<i>both</b>italic</i><ul><li>a<li>b</ul>"
                       "<table><tr><td>x<td>y</table>";
    DomNode *d = html_parse(html, (int)strlen(html));
    CHECK(count_tag(d, "p") == 2);
    CHECK(count_tag(d, "li") == 2);
    CHECK(count_tag(d, "td") == 2);
    CHECK(count_tag(d, "div") == 1);
    CHECK(count_tag(d, "table") == 1);
    CHECK(count_tag(d, "tr") == 1);
    dom_free_tree(d);
}
static void test_attrs_and_case(void) {
    const char *html = "<DIV ID='Main' CLASS=box data-x=1 hidden><IMG SRC=/a.png ALT=\"hi\">";
    DomNode *d = html_parse(html, (int)strlen(html));
    DomNode *div = first_tag(d, "div");
    CHECK(div != 0);
    CHECK(attr(div, "id") && attr(div, "id")[0] == 'M');
    CHECK(attr(div, "class") && attr(div, "class")[0] == 'b');
    CHECK(attr(div, "data-x") && attr(div, "data-x")[0] == '1');
    CHECK(attr(div, "hidden") != 0);
    DomNode *img = first_tag(d, "img");
    CHECK(img && img->src[0] == '/' && attr(img, "alt") && attr(img, "alt")[0] == 'h');
    dom_free_tree(d);
}
static void test_void_and_selfclose(void) {
    const char *html = "<p>a<br>b<hr/>c<input value=x></p><div/>tail";
    DomNode *d = html_parse(html, (int)strlen(html));
    CHECK(count_tag(d, "br") == 1);
    CHECK(count_tag(d, "hr") == 1);
    CHECK(count_tag(d, "input") == 1);
    DomNode *inp = first_tag(d, "input");
    CHECK(inp && inp->value[0] == 'x');
    dom_free_tree(d);
}
static void test_comments_and_doctype(void) {
    const char *html = "<!DOCTYPE html><!-- a comment with <tags> --><html><body><p>ok</p></body></html>";
    DomNode *d = html_parse(html, (int)strlen(html));
    DomNode *p = first_tag(d, "p");
    CHECK(p && p->first_child && p->first_child->text[0] == 'o');
    dom_free_tree(d);
}
static void test_script_style_raw(void) {
    const char *html = "<script>if (a < b && c > d) { x = \"</div>\"; }</script>"
                       "<style>p { color: red; } /* <b> */</style><p>after</p>";
    DomNode *d = html_parse(html, (int)strlen(html));
    DomNode *s = first_tag(d, "script");
    CHECK(s && s->first_child && s->first_child->text);
    const char *code = s->first_child->text;
    CHECK(has_sub(code, "a < b"));
    DomNode *st = first_tag(d, "style");
    CHECK(st && st->first_child && has_sub(st->first_child->text, "color: red"));
    CHECK(count_tag(d, "div") == 0);
    CHECK(count_tag(d, "p") == 1);
    dom_free_tree(d);
}
static void test_dom_queries(void) {
    const char *html = "<div id=root class='a b'><span class=x></span><span class=x id=two></span>"
                       "<p data-k=v>t</p></div>";
    DomNode *d = html_parse(html, (int)strlen(html));
    CHECK(dom_get_element_by_id(d, "two") != 0);
    CHECK(dom_get_element_by_id(d, "nope") == 0);
    DomNode *out[8]; int n = 0;
    dom_query_all(d, "span", out, 8, &n);
    CHECK(n == 2);
    dom_query_all(d, ".x", out, 8, &n);
    CHECK(n == 2);
    dom_query_all(d, "#root > span", out, 8, &n);
    CHECK(n == 2);
    dom_query_all(d, "div span", out, 8, &n);
    CHECK(n == 2);
    dom_query_all(d, "[data-k=v]", out, 8, &n);
    CHECK(n == 1);
    dom_query_all(d, "div.a", out, 8, &n);
    CHECK(n == 1);
    dom_free_tree(d);
}
static void test_dom_mutation(void) {
    const char *html = "<ul id=list><li>one</li></ul>";
    DomNode *d = html_parse(html, (int)strlen(html));
    DomNode *ul = dom_get_element_by_id(d, "list");
    DomNode *li2 = dom_create_element("li");
    dom_append_child(ul, li2);
    CHECK(count_tag(d, "li") == 2);
    DomNode *txt = dom_create_text("two", 3);
    dom_append_child(li2, txt);
    dom_set_attribute(li2, "class", "item");
    CHECK(dom_has_class(li2, "item"));
    dom_remove_attribute(li2, "class");
    CHECK(!dom_has_class(li2, "item"));
    DomNode *first = ul->first_child;
    dom_insert_before(ul, dom_create_element("li"), first);
    CHECK(count_tag(d, "li") == 3);
    dom_remove_child(ul, first);
    CHECK(count_tag(d, "li") == 2);
    dom_free_tree(d);
}
static void test_inner_html(void) {
    const char *html = "<div id=host></div>";
    DomNode *d = html_parse(html, (int)strlen(html));
    DomNode *host = dom_get_element_by_id(d, "host");
    const char *frag = "<b>hi</b><i>there</i>";
    dom_set_inner_html(host, frag, (int)strlen(frag));
    CHECK(count_tag(d, "b") == 1 && count_tag(d, "i") == 1);
    dom_set_inner_html(host, "", 0);
    CHECK(count_tag(d, "b") == 0);
    dom_free_tree(d);
}
static void test_css_cascade(void) {
    const char *html = "<div id=d class=c><p class=c id=p style='color:#00ff00'>x</p></div>";
    DomNode *d = html_parse(html, (int)strlen(html));
    const char *css = "p { color: #ff0000; font-size: 20px; } .c { color: #0000ff; } #p { color: #000000; }";
    css_apply_styles(d, css);
    DomNode *p = dom_get_element_by_id(d, "p");
    /* inline style must win over author rules */
    CHECK(p->style.color == 0x00ff00);
    CHECK(p->style.font_size >= 2);
    dom_free_tree(d);
}
static void test_css_selector_match(void) {
    const char *html = "<div><a class=link href=/x>1</a><a>2</a></div>";
    DomNode *d = html_parse(html, (int)strlen(html));
    DomNode *a1 = first_tag(d, "a");
    CHECK(css_match_one(a1, ".link", 5));
    CHECK(css_match_one(a1, "div .link", 9));
    CHECK(css_match_one(a1, "div > a", 7));
    CHECK(css_match_one(a1, "a[href]", 7));
    CHECK(css_match_one(a1, "a:first-child", 13));
    CHECK(!css_match_one(a1->next_sibling, ".link", 5));
    dom_free_tree(d);
}
static void test_css_units(void) {
    const char *html = "<div id=d></div>";
    DomNode *d = html_parse(html, (int)strlen(html));
    const char *css = "#d { width: 50%; height: 100px; margin: 10px 20px 30px 40px; padding: 5px; border: 2px solid #123456; }";
    css_apply_styles(d, css);
    DomNode *div = dom_get_element_by_id(d, "d");
    CHECK(div->style.height == 100);
    CHECK(div->style.margin_top == 10 && div->style.margin_right == 20);
    CHECK(div->style.margin_bottom == 30 && div->style.margin_left == 40);
    CHECK(div->style.padding_top == 5 && div->style.padding_left == 5);
    CHECK(div->style.border_width == 2);
    dom_free_tree(d);
}
int main(void) {
    test_entities();
    test_trade_entity_and_viewport_units();
    test_malformed();
    test_attrs_and_case();
    test_void_and_selfclose();
    test_comments_and_doctype();
    test_script_style_raw();
    test_dom_queries();
    test_dom_mutation();
    test_inner_html();
    test_css_cascade();
    test_css_selector_match();
    test_css_units();
    if (failures) { printf("FAILED: %d HTML/CSS assertions\n", failures); return 1; }
    printf("PASS: HTML tokenizer/DOM (entities, malformed, attrs, raw text) and CSS cascade/selectors/units\n");
    return 0;
}
