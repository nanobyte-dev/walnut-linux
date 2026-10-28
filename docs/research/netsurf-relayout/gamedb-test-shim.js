// Test-only ES5 shim for NetSurf 3.11: querySelectorAll (simple selectors, descendant combinator),
// URLSearchParams, history.replaceState. Not part of the relayout patch.
(function () {
  function parse(compound) {
    var m, r = { tag: null, id: null, cls: [], attrs: [] }, re = /([a-zA-Z][\w-]*)|#([\w-]+)|\.([\w-]+)|\[([\w-]+)(?:=["']?([^\]"']*)["']?)?\]/g;
    while ((m = re.exec(compound)) !== null) {
      if (m[1]) r.tag = m[1].toUpperCase();
      else if (m[2]) r.id = m[2];
      else if (m[3]) r.cls.push(m[3]);
      else r.attrs.push([m[4], m[5]]);
    }
    return r;
  }
  function match(el, c) {
    if (el.nodeType !== 1) return false;
    if (c.tag && String(el.tagName || el.nodeName).toUpperCase() !== c.tag) return false;
    if (c.id && el.getAttribute("id") !== c.id) return false;
    var k = " " + (el.getAttribute("class") || "") + " ";
    for (var i = 0; i < c.cls.length; i++) if (k.indexOf(" " + c.cls[i] + " ") < 0) return false;
    for (i = 0; i < c.attrs.length; i++) {
      var v = el.getAttribute(c.attrs[i][0]);
      if (v === null || v === undefined) return false;
      if (c.attrs[i][1] !== undefined && c.attrs[i][1] !== "" && v !== c.attrs[i][1]) return false;
    }
    return true;
  }
  function matchChain(el, chain, root) {
    if (!match(el, chain[chain.length - 1])) return false;
    var j = chain.length - 2, p = el.parentNode;
    while (j >= 0 && p && p !== root) { if (match(p, chain[j])) j--; p = p.parentNode; }
    if (j >= 0 && p === root && root.nodeType === 1 && match(root, chain[j])) j--;
    return j < 0;
  }
  function qsa(sel) {
    var chain = sel.replace(/^\s+|\s+$/g, "").split(/\s+/).map(parse), out = [], root = this;
    (function walk(n) {
      for (var c = n.firstChild; c; c = c.nextSibling) {
        if (c.nodeType === 1) { if (matchChain(c, chain, root)) out.push(c); walk(c); }
      }
    })(this.nodeType === 9 ? this.documentElement : this);
    if (this.nodeType === 9 && matchChain(this.documentElement, chain, null)) out.unshift(this.documentElement);
    return out;
  }
  // put it on the prototype that elements and the document share
  // NetSurf's own querySelectorAll stubs sit on several prototypes: override each one
  function qs(s) { return qsa.call(this, s)[0] || null; }
  [document, document.documentElement, document.createElement("div"), document.createElement("input"),
   document.createElement("button"), document.createElement("fieldset")].forEach(function (o) {
    try { if (o === document) { Object.defineProperty(document, "querySelectorAll", { value: qsa, configurable: true, writable: true });
      Object.defineProperty(document, "querySelector", { value: qs, configurable: true, writable: true }); } } catch (e) { window.__shimerr = (window.__shimerr || "") + " doc:" + e; }
    for (var p = Object.getPrototypeOf(o); p && p !== Object.prototype; p = Object.getPrototypeOf(p)) {
      try { Object.defineProperty(p, "querySelectorAll", { value: qsa, configurable: true, writable: true }); } catch (e) {  }
      try { Object.defineProperty(p, "querySelector", { value: qs, configurable: true, writable: true }); } catch (e) { }
    }
  });

  window.URLSearchParams = function (s) {
    this.k = []; this.v = [];
    s = (s || "").replace(/^\?/, "");
    var parts = s ? s.split("&") : [];
    for (var i = 0; i < parts.length; i++) {
      var kv = parts[i].split("=");
      this.k.push(decodeURIComponent(kv[0])); this.v.push(decodeURIComponent((kv[1] || "").replace(/\+/g, " ")));
    }
  };
  window.URLSearchParams.prototype.get = function (n) { var i = this.k.indexOf(n); return i < 0 ? null : this.v[i]; };
  window.URLSearchParams.prototype.set = function (n, v) { var i = this.k.indexOf(n); if (i < 0) { this.k.push(n); this.v.push(v); } else this.v[i] = v; };
  window.URLSearchParams.prototype.toString = function () {
    var o = [];
    for (var i = 0; i < this.k.length; i++) o.push(encodeURIComponent(this.k[i]) + "=" + encodeURIComponent(this.v[i]));
    return o.join("&");
  };
  if (!window.history || !window.history.replaceState)
    Object.defineProperty(window, "history", { value: { replaceState: function () {} }, configurable: true });
})();
